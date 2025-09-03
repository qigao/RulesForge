#!/bin/bash

# Drills WebAssembly Build Script
# This script helps build the WebAssembly demo for the Drills Rule Engine

set -e  # Exit on any error

echo "🛠️ Drills WebAssembly Builder"
echo "=============================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check for Emscripten
print_status "Checking for Emscripten..."
if ! command -v emcmake &> /dev/null; then
    print_error "Emscripten not found in PATH"
    print_error "Please install Emscripten SDK:"
    echo "  git clone https://github.com/emscripten-core/emsdk.git"
    echo "  cd emsdk"
    echo "  ./emsdk install latest"
    echo "  ./emsdk activate latest"
    echo "  source ./emsdk_env.sh"
    exit 1
fi

print_success "Emscripten found: $(emcmake --version | head -1)"

# Create build directory
BUILD_DIR="build"
if [ -d "$BUILD_DIR" ]; then
    print_warning "Build directory exists. Cleaning..."
    rm -rf "$BUILD_DIR"
fi

print_status "Creating build directory..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
print_status "Configuring with CMake..."
if ! emcmake cmake ..; then
    print_error "CMake configuration failed"
    exit 1
fi

# Build the project
print_status "Building WebAssembly module..."
if ! emmake make -j$(nproc); then
    print_error "Build failed"
    exit 1
fi

# Copy web files to build directory
print_status "Copying web files..."
cp ../web/* . 2>/dev/null || print_warning "No web files to copy"

# Verify output files
print_status "Verifying output files..."
if [ -f "drills.js" ] && [ -f "drills.wasm" ]; then
    print_success "WebAssembly build completed successfully!"
    echo ""
    echo -e "${GREEN}📁 Generated files:${NC}"
    echo "  - drills.js (WebAssembly loader)"
    echo "  - drills.wasm (WebAssembly binary)"
    echo ""
    echo -e "${GREEN}🚀 To run the demo:${NC}"
    echo "  cd $BUILD_DIR"
    echo "  python3 -m http.server 8000"
    echo "  # Then open http://localhost:8000 in your browser"
    echo ""
    echo -e "${GREEN}📊 Build info:${NC}"
    echo "  - JavaScript size: $(ls -lh drills.js | awk '{print $5}')"
    echo "  - WebAssembly size: $(ls -lh drills.wasm | awk '{print $5}')"
else
    print_error "Expected output files not found"
    exit 1
fi

cd ..
print_success "Build completed! 🎉"
