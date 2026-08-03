

#include "ma_transport_serial.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

extern "C" {
#include <console_io.h>
#include <hx_drv_scu.h>
#include <hx_drv_uart.h>
}

#include <porting/ma_misc.h>
#include <porting/ma_osal.h>

#include <core/utils/ma_ringbuffer.hpp>

#include "ma_config_board.h"

// TX ring for UART_1 (PB6/PB7 — the XIAO/ESP32 header, see esp_node/README.md).
//
// 4 KB is enough for AT responses and results-only INVOKE events. Streaming
// base64 JPEG frames to a companion MCU needs a frame-sized ring or send()
// spends every frame in the ring-full path. Raise it from the board config
// only when the ESP32 link actually carries images — it comes straight out of
// the 336 KB FreeRTOS heap the AT/JSON event path already competes for.
#ifndef MA_TRANSPORT_SERIAL_TX_RING_SIZE
#define MA_TRANSPORT_SERIAL_TX_RING_SIZE (4 * 1024)
#endif

namespace ma {

static SPSCRingBuffer<char>* _rb_rx   = nullptr;
static SPSCRingBuffer<char>* _rb_tx   = nullptr;
static char*                 _rx_buf  = nullptr;
static char*                 _tx_buf  = nullptr;
static volatile bool         _tx_busy = false;
static Mutex                 _tx_mutex;
static DEV_UART*             _uart      = nullptr;
static volatile bool         _is_opened = false;

static void _uart_dma_recv(void*) {
    if (!_is_opened || !_rb_rx || !_rx_buf || !_uart) {
        return;
    }
    SCB_CleanDCache_by_Addr(_rx_buf, 1);
    _rb_rx->push(_rx_buf, 1);
    _uart->uart_read_udma(_rx_buf, 1, reinterpret_cast<void*>(_uart_dma_recv));
}

static void _uart_dma_send(void*);

// Claim the TX DMA and start it if it is idle and there is queued data.
//
// Mirrors Console's _tx_kick(). The check-and-claim MUST be atomic against
// _uart_dma_send() below, which runs in ISR context and clears _tx_busy
// whenever it finds the ring empty. The old code did a bare
// `if (!_tx_busy) { _tx_busy = true; ... }` from task context: if the ISR
// sampled an empty ring just before send() pushed new bytes, send() would then
// observe _tx_busy still set, yield, and return — leaving data in the ring with
// no DMA in flight and nothing left to re-arm it. The stream stops dead. Only
// reachable with payloads larger than the ring, which is exactly what streaming
// base64 JPEG frames to the ESP32 over this UART does.
static void _tx_kick() {
    if (!_is_opened || !_rb_tx || !_tx_buf || !_uart) {
        return;
    }
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const bool claimed = !_tx_busy && !_rb_tx->empty();
    if (claimed) {
        _tx_busy = true;
    }
    __set_PRIMASK(primask);
    if (!claimed) {
        return;
    }
    size_t n = std::min(_rb_tx->size(), static_cast<size_t>(4095));
    n        = _rb_tx->pop(_tx_buf, n);
    SCB_CleanDCache_by_Addr(_tx_buf, n);
    _uart->uart_write_udma(_tx_buf, n, reinterpret_cast<void*>(_uart_dma_send));
}

static void _uart_dma_send(void*) {
    if (!_is_opened || !_rb_tx || !_tx_buf || !_uart) {
        _tx_busy = false;
        return;
    }
    size_t remain = std::min(_rb_tx->size(), static_cast<size_t>(4095));
    if ((_tx_busy = (remain != 0))) {
        remain = _rb_tx->pop(_tx_buf, remain);
        SCB_CleanDCache_by_Addr(_tx_buf, remain);
        _uart->uart_write_udma(_tx_buf, remain, reinterpret_cast<void*>(_uart_dma_send));
    }
}

Serial::Serial() : Transport(MA_TRANSPORT_SERIAL) {}

Serial::~Serial() { deInit(); }

ma_err_t Serial::init(const void* config) {
    if (_is_opened || m_initialized) {
        return MA_OK;
    }

    (void)config;

    if (!(hx_drv_scu_set_PB6_pinmux(SCU_PB6_PINMUX_UART1_RX, 0) == 0 &&
          hx_drv_scu_set_PB7_pinmux(SCU_PB7_PINMUX_UART1_TX, 0) == 0 &&
          hx_drv_uart_init(USE_DW_UART_1, HX_UART1_BASE) == 0)) {
        return MA_EIO;
    }

    _uart = hx_drv_uart_get_dev(USE_DW_UART_1);
    if (_uart == nullptr) {
        return MA_EIO;
    }

    // This is UART_1 (separate physical peripheral from Console's UART_0,
    // different pins — PB6/PB7). Turned out NOT to be the transport wired
    // to the CH343 USB-serial bridge we actually use (that's UART_0, see
    // board.c's console_setup() and HANDOVER.md section 8) — changing this
    // had no observable effect during that investigation. Left at 921600.
    int ret = _uart->uart_open(UART_BAUDRATE_921600);
    if (ret != 0) {
        return MA_EIO;
    }

    if (_rx_buf == nullptr) {
        _rx_buf = new (std::align_val_t{32}) char[32];
    }

    if (_tx_buf == nullptr) {
        _tx_buf = new (std::align_val_t{32}) char[4096];
    }

    if (_rb_rx == nullptr) {
        _rb_rx = new SPSCRingBuffer<char>(4096);
    }

    if (_rb_tx == nullptr) {
        // Default 4 KB, not the 48 KB the Console uses. This is UART_1
        // (PB6/PB7), which is NOT the port the CH343 USB bridge exposes. The
        // 48 KB it used to reserve came straight out of the 336 KB FreeRTOS
        // heap that the AT/JSON event path has to share, leaving only ~60 KB
        // free at steady state; encoding one detailed frame needs several
        // simultaneous copies of the payload and was overrunning that.
        //
        // Raise MA_TRANSPORT_SERIAL_TX_RING_SIZE when a companion MCU on the
        // XIAO header streams image payloads over this UART.
        _rb_tx = new SPSCRingBuffer<char>(MA_TRANSPORT_SERIAL_TX_RING_SIZE);
    }

    if (!_rx_buf || !_tx_buf || !_rb_rx || !_rb_tx) {
        return MA_ENOMEM;
    }

    std::memset(_rx_buf, 0, 32);
    std::memset(_tx_buf, 0, 4096);

    _is_opened = m_initialized = true;

    _uart->uart_read_udma(_rx_buf, 1, reinterpret_cast<void*>(_uart_dma_recv));

    return MA_OK;
}

void Serial::deInit() {
    if (!_is_opened || !m_initialized) {
        return;
    }

    while (_tx_busy) {
        ma::Thread::yield();
    }

    if (_uart) {
        _uart->uart_close();
        hx_drv_uart_deinit(USE_DW_UART_1);
        _uart = nullptr;
    }

    if (_rb_rx) {
        delete _rb_rx;
        _rb_rx = nullptr;
    }

    if (_rb_tx) {
        delete _rb_tx;
        _rb_tx = nullptr;
    }

    if (_rx_buf) {
        delete[] _rx_buf;
        _rx_buf = nullptr;
    }

    if (_tx_buf) {
        delete[] _tx_buf;
        _tx_buf = nullptr;
    }

    _is_opened = m_initialized = false;
}

size_t Serial::available() const { return _rb_rx->size(); }

size_t Serial::send(const char* data, size_t length) {
    if (!m_initialized || data == nullptr || length == 0) {
        return 0;
    }

    Guard guard(_tx_mutex);

    size_t sent = 0;

    // Bound the ring-full wait. Unbounded, a TX DMA that stops draining wedges
    // the Executor task here forever with no output at all. Give up and drop
    // the frame instead of hanging the device. Same reasoning as Console::send.
    unsigned           stall_spins    = 0;
    constexpr unsigned kMaxStallSpins = 200000;

    while (length) {
        const size_t pushed = _rb_tx->push(data + sent, length);
        length -= pushed;
        sent += pushed;

        _tx_kick();

        if (pushed == 0) {
            if (++stall_spins >= kMaxStallSpins) {
                break;
            }
            ma::Thread::yield();
        } else {
            stall_spins = 0;
        }
    }

    return sent;
}

size_t Serial::flush() {
    if (!m_initialized) {
        return -1;
    }

    // send() only guarantees the data is copied into _rb_tx; the DMA drains it
    // asynchronously. The invoke loop calls flush() after every frame to pace
    // itself to the link so the ring never fills — that only works if flush()
    // actually waits. Bounded so a stuck _tx_busy degrades into a dropped frame
    // instead of wedging the Executor task.
    constexpr int kMaxYields = 200000;
    int           spins      = 0;
    while ((_tx_busy || !_rb_tx->empty()) && spins++ < kMaxYields) {
        ma::Thread::yield();
    }

    return 0;
}

size_t Serial::receive(char* data, size_t length) {
    if (!m_initialized || length == 0) {
        return 0;
    }

    return _rb_rx->pop(data, length);
}

size_t Serial::receiveIf(char* data, size_t length, char delimiter) {
    if (!m_initialized || length == 0) {
        return 0;
    }

    return _rb_rx->popIf(data, length, delimiter);
}

}  // namespace ma
