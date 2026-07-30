# Technical Reference Guide: Himax WiseEye2 (WE2) Ecosystem Migration to YOLO26
**Target Architecture:** Arm Ethos-U55 microNPU 
**Project SDK Context:** `sscma-example-we2` / Himax WiseEye Plus

---

## 1. Architectural Impact of YOLO26
Ultralytics YOLO26 utilizes an **end-to-end NMS-free dual-head** training layout. This introduces a distinct architectural branching path when planning your C++ post-processing codebase:

### Path A: Native NMS-Free (Default Weights Optimization)
* **TFLite Model Tensor Output:** `(N, 300, 6)`
* **Firmware Implementation:** You must completely strip out the standard Non-Maximum Suppression (NMS) loop calculations from your platform C++ source code. The hardware tensor parser maps raw coordinate metrics straight to your final inference results.

### Path B: Dense Multi-Box Format (Legacy Post-Processing Compatibility)
* **TFLite Model Tensor Output:** `(N, classes + 4, 8400)`
* **Firmware Implementation:** Retains compatibility with legacy YOLOv8/YOLO11 C++ tensor parsing logic and executes hardware/software NMS filtering loops.
* **Export Requirements:** You do not need to retrain your weights. Pass the `end2end=False` parameter via the Ultralytics Python API to force the exporter to leverage the alternative "One-to-Many" head natively embedded inside your `.pt` checkpoint file:

```python
from ultralytics import YOLO

# Load standard trained YOLO26 checkpoint
model = YOLO("path/to/yolo26_best.pt")

# Export to INT8 TFLite dropping the NMS-free head
model.export(format="tflite", end2end=False, int8=True, data="your_dataset.yaml")
```

---

## 2. Low-Level Core Driver & Vela Compiler Coexistence Matrix
The baseline Himax firmware utilizes compilation tag layout `tflmtag2412_u55tag2411`. The **Arm Vela Compiler** version used in your development environment must align exactly with your low-level runtime drivers to avoid hard firmware memory faults or "Unsupported command stream" errors.

| Target NPU Driver Tag | Core Release Window | Max Supported Vela Compiler Version | Driver Provenance |
| :--- | :--- | :--- | :--- |
| **`u55tag2411`** (Stock) | November 2024 | `3.11.0` | Arm Ethos-U Pack v1.24.11 |
| **`u55tag2511`** (Upgraded) | November 2025 | Controlled via Manifest Hash | Arm Ethos-U Pack v1.25.x+ |

### Pinning the Local Stock Vela Environment
```bash
pip uninstall ethos-u-vela
pip install ethos-u-vela==3.11.0
```

### Vela NPU Operator Compiling Arguments (Himax WE2 Layout)
```bash
vela your_yolo26_int8.tflite \
  --accelerator-config ethos-u55-64 \
  --system-config=Ethos_U55_High_Delay \
  --memory-mode=Dedicated_Sram \
  --output-dir=./output
```
*Note: If you run your old YOLO11 models on an upgraded driver, they will execute properly, but you must pass the uncompiled YOLO11 `.tflite` file through your newly upgraded Vela compiler version so the operator command stream matches the updated driver format.*

---

## 3. Manual Driver Extraction and Ecosystem Synchronization

### Unpacking Arm CMSIS Driver Packs
The official hardware abstraction layers are distributed by Arm via CMSIS `.pack` files (e.g., from Arm Keil portals). These archives are standard ZIP files with a modified extension.

1. **Format Alteration & Decompression:**
   ```bash
   mv ARM.ethos-u-core-driver.1.26.2.pack ARM.ethos-u-core-driver.1.26.2.zip
   unzip ARM.ethos-u-core-driver.1.26.2.zip -d ethos_u_extracted
   ```
2. **File Sourcing Paths:** The raw hardware driver implementation blocks are located within the unpacked folder subdirectories:
   * **Source Implementation Files:** `ethos_u_extracted/core_driver/src/ethosu_device_u55_u65.c`
   * **Header Definitions:** `ethos_u_extracted/core_driver/include/`

### The Role of Manifest Blueprints (`*.json`)
Arm utilizes global synchronization manifest files (e.g., `25.11.json`) within their GitLab artificial-intelligence tree. These JSON files act as a master versioning map. They track the exact validated Git commit hashes and branch versions across decoupled development repositories (`ethos-u-core-driver`, `ethos-u-vela`, `tensorflow-lite-micro`) needed to guarantee system-wide integration stability.

---

## 4. Build System Environment Modification (`*.mk`)
The global compilation toolchain relies on localized GNU Makefile modules (like `tflmtag2412_u55tag2411.mk`) within the `sscma-example-we2` environment to map library paths.

### Function of the Configuration Module
* Inject deep include paths for low-level header bindings (e.g., `micro_interpreter.h`).
* Declare target subsystem directory paths (`TFLM_TAG` and `ETHOS_U_TAG`).
* Flag platform-specific compiler settings (`-DETHOS_U`), ensuring the interpreter offloads math blocks directly to the physical microNPU instead of defaulting to sluggish CPU software emulation.

### Migration Pipeline for Custom Code Base Upgrades
When changing versions, do not create this file from scratch. Instead, copy and edit an existing module:

1. **Duplicate and Relabel:**
   ```bash
   cp tflmtag2412_u55tag2411.mk tflmtag2511_u55tag2511.mk
   ```
2. **Re-map Core Variable Keys:** Adjust the internal pathing strings inside your new file to mirror your updated local repository trees:
   ```makefile
   # Custom Upgraded Architecture Identifiers
   TFLM_TAG := tflmtag2511
   ETHOS_U_TAG := u55tag2511
   ```
3. **Execute Core Code Compilation:** Pass your custom compilation layout flag directly to your local GCC toolchain:
   ```bash
   make APP=your_yolo26_app TFLM_DRIVER=tflmtag2511_u55tag2511
   ```
