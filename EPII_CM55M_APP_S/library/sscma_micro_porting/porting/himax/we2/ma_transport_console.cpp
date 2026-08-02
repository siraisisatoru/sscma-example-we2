

#include "ma_transport_console.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

extern "C" {
#include <console_io.h>
#include <hx_drv_uart.h>
}

#include <porting/ma_misc.h>
#include <porting/ma_osal.h>

#include <core/utils/ma_ringbuffer.hpp>

#include "ma_config_board.h"

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

#ifdef MA_DEBUG_TX_TRACE
// Counters for diagnosing the large-payload TX stall. Read from flush()'s
// timeout path, which is reached only when the DMA has stopped making progress.
static volatile uint32_t _dbg_isr    = 0;  // _uart_dma_send() entries
static volatile uint32_t _dbg_rearm  = 0;  // ISR re-armed another chunk
static volatile uint32_t _dbg_kick   = 0;  // _tx_kick() claimed and started a transfer
static volatile uint32_t _dbg_sends  = 0;  // Console::send() calls
static volatile uint32_t _dbg_full   = 0;  // send() saw a full ring and yielded
#endif

// Claim the TX DMA and start it if it is idle and there is queued data.
//
// The check-and-claim MUST be atomic against _uart_dma_send() below, which runs
// in ISR context and clears _tx_busy whenever it finds the ring empty. The old
// code did a bare `if (!_tx_busy) { _tx_busy = true; ... }` from task context:
// if the ISR sampled an empty ring just before send() pushed new bytes, send()
// would then observe _tx_busy still set, yield, and return — leaving data in the
// ring with no DMA in flight and nothing left to re-arm it. The stream stops
// dead and the device looks frozen. Larger payloads make this far more likely
// (many more push/ring-full/yield cycles per frame), which is why it showed up
// when streaming detailed frames and not blank ones.
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
#ifdef MA_DEBUG_TX_TRACE
    ++_dbg_kick;
#endif
    size_t n = std::min(_rb_tx->size(), static_cast<size_t>(4095));
    n        = _rb_tx->pop(_tx_buf, n);
    SCB_CleanDCache_by_Addr(_tx_buf, n);
    _uart->uart_write_udma(_tx_buf, n, reinterpret_cast<void*>(_uart_dma_send));
}

static void _uart_dma_send(void*) {
#ifdef MA_DEBUG_TX_TRACE
    ++_dbg_isr;
#endif
    if (!_is_opened || !_rb_tx || !_tx_buf || !_uart) {
        _tx_busy = false;
        return;
    }
    size_t remain = std::min(_rb_tx->size(), static_cast<size_t>(4095));
    if ((_tx_busy = (remain != 0))) {
#ifdef MA_DEBUG_TX_TRACE
        ++_dbg_rearm;
#endif
        remain = _rb_tx->pop(_tx_buf, remain);
        SCB_CleanDCache_by_Addr(_tx_buf, remain);
        _uart->uart_write_udma(_tx_buf, remain, reinterpret_cast<void*>(_uart_dma_send));
    }
}

Console::Console() : Transport(MA_TRANSPORT_CONSOLE) {}

Console::~Console() { deInit(); }

ma_err_t Console::init(const void* config) {
    if (_is_opened || m_initialized) {
        return MA_OK;
    }

    (void)config;

    _uart = hx_drv_uart_get_dev(USE_DW_UART_0);
    if (_uart == nullptr) {
        return MA_EIO;
    }

    // NOTE: this call doesn't actually control the effective baud rate for
    // this board's physical UART_0 — the FIRST uart_open() on this device
    // happens earlier in board_init() (board.c's console_setup() call,
    // before app_main()/this init() ever run), and the driver silently
    // ignores a second uart_open() on an already-open device. See board.c
    // and HANDOVER.md section 8 (streaming-freeze investigation) before
    // touching this value — a lower baud was tried there and made image
    // transport corruption worse, not better.
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
        _rb_tx = new SPSCRingBuffer<char>(48 * 1024);
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

void Console::deInit() {
    if (!_is_opened || !m_initialized) {
        return;
    }

    while (_tx_busy) {
        ma::Thread::yield();
    }

    if (_uart) {
        _uart->uart_close();
        hx_drv_uart_deinit(USE_DW_UART_0);
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

size_t Console::available() const { return _rb_rx->size(); }

size_t Console::send(const char* data, size_t length) {
    if (!m_initialized || data == nullptr || length == 0) {
        return 0;
    }

    Guard guard(_tx_mutex);

#ifdef MA_DEBUG_TX_TRACE
    ++_dbg_sends;
#endif
    size_t sent = 0;

    // Bound the ring-full wait. This loop used to be unbounded: if the TX DMA
    // ever stops draining the ring, send() spins here forever, and because it
    // never returns, flush() (and its stall diagnostic) is never reached — the
    // Executor task is wedged with no output at all, which is exactly how this
    // failure presents. Give up and drop the frame instead of hanging the device.
    unsigned stall_spins = 0;
    constexpr unsigned kMaxStallSpins = 200000;

    while (length) {
        const size_t pushed = _rb_tx->push(data + sent, length);
        length -= pushed;
        sent += pushed;

        if (pushed == 0) {
            if (++stall_spins >= kMaxStallSpins) {
                ma_printf("\r\n!! TX SEND STALL busy=%d ring=%u want=%u"
#ifdef MA_DEBUG_TX_TRACE
                          " isr=%u rearm=%u kick=%u send=%u"
#endif
                          "\r\n",
                          static_cast<int>(_tx_busy),
                          static_cast<unsigned>(_rb_tx->size()),
                          static_cast<unsigned>(length)
#ifdef MA_DEBUG_TX_TRACE
                          ,
                          static_cast<unsigned>(_dbg_isr),
                          static_cast<unsigned>(_dbg_rearm),
                          static_cast<unsigned>(_dbg_kick),
                          static_cast<unsigned>(_dbg_sends)
#endif
                );
                break;
            }
        } else {
            stall_spins = 0;
        }

        // Kick after every push, including the final one — see _tx_kick() for
        // the race that made the old "kick only when _tx_busy looks clear"
        // check drop the last chunk and stall the stream permanently.
        _tx_kick();

        if (length) {
#ifdef MA_DEBUG_TX_TRACE
            ++_dbg_full;
#endif
            // Ring is full; the DMA is draining it. Yield so the ISR can make
            // progress. This is the backpressure that paces a producer faster
            // than the UART.
            ma::Thread::yield();
        }
    }

    // Final safety kick: covers the case where the last push exactly filled the
    // ring and the ISR cleared _tx_busy between that push and the kick above.
    _tx_kick();

    return sent;
}

size_t Console::flush() {
    if (!m_initialized) {
        return -1;
    }

    // send() only guarantees the data is copied into _rb_tx; the DMA drains it
    // asynchronously. Wait for the ring buffer to empty and the DMA to go idle.
    //
    // The invoke loop calls this after every frame so the TX ring never fills
    // and send()'s ring-full path is never entered — see invoke.hpp. Bounded
    // rather than an open spin so a stuck _tx_busy degrades into a dropped
    // frame instead of wedging the Executor task forever.
    constexpr int kMaxYields = 200000;
    int spins = 0;
    while ((_tx_busy || !_rb_tx->empty()) && spins++ < kMaxYields) {
        ma::Thread::yield();
    }

#ifdef MA_DEBUG_TX_TRACE
    if (spins >= kMaxYields) {
        // Reached only when the DMA has stopped making progress. Printing here
        // is safe precisely because the DMA is dead: ma_printf() goes out over
        // the polled console_putchar() path, not the ring/DMA path being dumped.
        ma_printf("\r\n!! TX STALL busy=%d ring=%u isr=%u rearm=%u kick=%u send=%u full=%u\r\n",
                  static_cast<int>(_tx_busy),
                  static_cast<unsigned>(_rb_tx->size()),
                  static_cast<unsigned>(_dbg_isr),
                  static_cast<unsigned>(_dbg_rearm),
                  static_cast<unsigned>(_dbg_kick),
                  static_cast<unsigned>(_dbg_sends),
                  static_cast<unsigned>(_dbg_full));
    }
#endif

    return 0;
}

size_t Console::receive(char* data, size_t length) {
    if (!m_initialized || length == 0) {
        return 0;
    }

    return _rb_rx->pop(data, length);
}

size_t Console::receiveIf(char* data, size_t length, char delimiter) {
    if (!m_initialized || length == 0) {
        return 0;
    }

    return _rb_rx->popIf(data, length, delimiter);
}

}  // namespace ma
