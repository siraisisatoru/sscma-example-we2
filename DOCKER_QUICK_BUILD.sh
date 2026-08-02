#!/bin/bash
#
# DOCKER_QUICK_BUILD.sh - One-command Docker build for tflmtag2605_u55tag2605 libraries
#
# Usage:
#   ./DOCKER_QUICK_BUILD.sh                  # Standard build
#   ./DOCKER_QUICK_BUILD.sh --interactive    # Interactive shell
#   ./DOCKER_QUICK_BUILD.sh --debug          # Debug build with symbols
#   ./DOCKER_QUICK_BUILD.sh --save-log       # Save build log
#

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_MODE="${1:-default}"
DOCKER_IMAGE_TAG="sscma-yolo26-build:26.05"

# Firmware image generation (runs inside the same container right after a
# successful make, using the Linux x86-64 we2_local_image_gen binary).
IMAGE_GEN_CMD="cd /workspace/we2_image_gen_local && \
                echo '📦 Generating firmware image...' && \
                cp /workspace/EPII_CM55M_APP_S/obj_epii_evb_icv30_bdv10/gnu_epii_evb_WLCSP65/EPII_CM55M_gnu_epii_evb_WLCSP65_s.elf input_case1_secboot/ && \
                ./we2_local_image_gen project_case1_blp_wlcsp.json && \
                echo '✅ Image generated: we2_image_gen_local/output_case1_sec_wlcsp/output.img'"

echo "╔════════════════════════════════════════════════════════════════╗"
echo "║     SSCMA YOLO26 Docker Build - tflmtag2605_u55tag2605        ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""

# Step 1: Check if Docker is installed
if ! command -v docker &> /dev/null; then
    echo "❌ Docker is not installed. Please install Docker first:"
    echo "   macOS: brew install docker (or Docker Desktop)"
    echo "   Linux: sudo apt-get install docker.io"
    exit 1
fi

echo "✓ Docker is installed"
echo ""

# Step 2: Build Docker image
echo "📦 Building Docker image: $DOCKER_IMAGE_TAG"
echo "   (This takes 3-5 minutes on first run, then uses cache)"
echo ""

docker build \
    -t "$DOCKER_IMAGE_TAG" \
    -f Dockerfile \
    "$PROJECT_ROOT" > /dev/null

echo "✓ Docker image built successfully"
echo ""

# Step 3: Run build based on mode
case "$BUILD_MODE" in
    --interactive)
        echo "🔧 Starting interactive build shell..."
        echo "   Commands available inside:"
        echo "     cd /workspace/EPII_CM55M_APP_S"
        echo "     make clean"
        echo "     make CMSIS_NN_LIB_FORCE_PREBUILT=n INFERENCE_FORCE_PREBUILT=n -j\$(nproc)"
        echo ""
        docker run \
            --rm \
            -it \
            -v "$PROJECT_ROOT:/workspace" \
            "$DOCKER_IMAGE_TAG" \
            bash
        ;;

    --debug)
        echo "🐛 Building with debug symbols (DEBUG=1)..."
        docker run \
            --rm \
            -v "$PROJECT_ROOT:/workspace" \
            "$DOCKER_IMAGE_TAG" \
            bash -c "
                cd /workspace/EPII_CM55M_APP_S && \
                echo '📝 Starting debug build...' && \
                make clean && \
                make CMSIS_NN_LIB_FORCE_PREBUILT=n \
                     INFERENCE_FORCE_PREBUILT=n \
                     DEBUG=1 \
                     -j\$(nproc) && \
                echo '✅ Debug build completed!' && \
                $IMAGE_GEN_CMD
            "
        ;;

    --save-log)
        echo "📝 Building with build log saved to build.log..."
        docker run \
            --rm \
            -v "$PROJECT_ROOT:/workspace" \
            "$DOCKER_IMAGE_TAG" \
            bash -c "
                cd /workspace/EPII_CM55M_APP_S && \
                echo '📝 Starting build (logging to /workspace/build.log)...' && \
                make clean && \
                make CMSIS_NN_LIB_FORCE_PREBUILT=y \
                     INFERENCE_FORCE_PREBUILT=y \
                     -j\$(nproc) 2>&1 | tee /workspace/build.log && \
                echo '✅ Build completed! Log saved to /workspace/build.log' && \
                $IMAGE_GEN_CMD
            "
        ;;

    *)
        echo "🚀 Starting standard build..."
        echo "   Libraries: cmsis_nn_ece5a3 + tflmtag2605_u55tag2605"
        echo "   Duration: 5-15 minutes (first run), 1-2 minutes (cached)"
        echo ""
        docker run \
            --rm \
            -v "$PROJECT_ROOT:/workspace" \
            "$DOCKER_IMAGE_TAG" \
            bash -c "
                cd /workspace/EPII_CM55M_APP_S && \
                echo '🏗️  Starting compilation...' && \
                make clean && \
                make CMSIS_NN_LIB_FORCE_PREBUILT=n \
                     INFERENCE_FORCE_PREBUILT=n \
                     -j\$(nproc) && \
                echo '✅ Build completed successfully!' && \
                $IMAGE_GEN_CMD
            "
        ;;
esac

# Step 4: Verify libraries were created
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "📊 BUILD VERIFICATION"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

if [ -f "$PROJECT_ROOT/EPII_CM55M_APP_S/prebuilt_libs/gnu/lib_cmsis_nn_ece5a3.a" ]; then
    echo "✅ CMSIS-NN library created:"
    ls -lh "$PROJECT_ROOT/EPII_CM55M_APP_S/prebuilt_libs/gnu/lib_cmsis_nn_ece5a3.a"
else
    echo "❌ CMSIS-NN library NOT found!"
    exit 1
fi

echo ""

if [ -f "$PROJECT_ROOT/EPII_CM55M_APP_S/prebuilt_libs/gnu/libtflmtag2605_u55tag2605_cmsisnn_gnu.a" ]; then
    echo "✅ TensorFlow Lite Micro library created:"
    ls -lh "$PROJECT_ROOT/EPII_CM55M_APP_S/prebuilt_libs/gnu/libtflmtag2605_u55tag2605_cmsisnn_gnu.a"
else
    echo "❌ TensorFlow Lite Micro library NOT found!"
    exit 1
fi

echo ""

if [ -f "$PROJECT_ROOT/EPII_CM55M_APP_S/obj_epii_evb_icv30_bdv10/gnu_epii_evb_WLCSP65/EPII_CM55M_gnu_epii_evb_WLCSP65_s.elf" ]; then
    echo "✅ Firmware ELF created:"
    ls -lh "$PROJECT_ROOT/EPII_CM55M_APP_S/obj_epii_evb_icv30_bdv10/gnu_epii_evb_WLCSP65/EPII_CM55M_gnu_epii_evb_WLCSP65_s.elf"
else
    echo "⚠️  Firmware ELF not found (may be expected if not configured)"
fi

echo ""

if [ -f "$PROJECT_ROOT/we2_image_gen_local/output_case1_sec_wlcsp/output.img" ]; then
    echo "✅ Firmware image created:"
    ls -lh "$PROJECT_ROOT/we2_image_gen_local/output_case1_sec_wlcsp/output.img"
else
    echo "❌ Firmware image NOT found!"
    exit 1
fi

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "✅ BUILD SUCCESSFUL!"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "Next steps:"
echo "  1. Commit libraries to git:"
echo "     git add EPII_CM55M_APP_S/prebuilt_libs/gnu/*.a"
echo "     git commit -m \"build: Docker-built libraries for 26.05 release\""
echo ""
echo "  2. Flash to device:"
echo "     python3 xmodem/xmodem_send.py --port=/dev/ttyACM0 --baudrate=921600 --file=we2_image_gen_local/output_case1_sec_wlcsp/output.img"
echo ""
