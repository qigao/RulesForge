# Drills WebAssembly Demo 🛠️

> WebAssembly build of the Drills Rule Engine demonstrating client-side rule processing in the browser

[![WebAssembly](https://img.shields.io/badge/WebAssembly-1.0-blue.svg)](https://webassembly.org/)
[![Emscripten](https://img.shields.io/badge/Emscripten-SDK-red.svg)](https://emscripten.org/)
[![Demo](https://img.shields.io/badge/Demo-Live-green.svg)](https://example.com/drills-wasm/)

## 🌟 Features

- ✅ **Browser-native Rule Engine**: Run C++ Drills engine directly in the browser via WebAssembly
- ✅ **Interactive Demo**: Complete web interface for testing rule engine functionality
- ✅ **Real-time Performance**: Live metrics and console logging
- ✅ **Sample Rule Sets**: Pre-built business rules for customer classification
- ✅ **JSON I/O**: Seamlessly handle JSON facts and query results
- ✅ **Memory Management**: Automatic resource cleanup and memory monitoring

## 🚀 Quick Start

### Prerequisites

1. **Emscripten SDK** - Install from [emscripten.org](https://emscripten.org/docs/getting_started/downloads.html)
   ```bash
   # Clone emscripten sdk
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   ./emsdk install latest
   ./emsdk activate latest
   source ./emsdk_env.sh
   ```

2. **Node.js & Serve** - For serving the demo locally
   ```bash
   npm install -g serve
   ```

### Build and Run

1. **Build WebAssembly Module**
   ```bash
   cd wasm
   mkdir build && cd build
   emcmake cmake ..
   emmake make
   ```

2. **Serve Demo**
   ```bash
   # From wasm/web directory
   serve . -p 3000
   ```

3. **Open Browser**
   ```
   http://localhost:3000
   ```

## 🎮 Demo Usage

### Step-by-Step Guide

1. **Initialize Engine** 🚀
   - Click "Initialize Engine" to load the WebAssembly module
   - Watch the status indicators turn green
   - Monitor load time in performance metrics

2. **Create Knowledge Base** 📚
   - Initialize an empty rule container
   - Required before loading rules or creating sessions

3. **Create Session** 🎯
   - Start an active session for rule processing
   - Enables fact insertion and rule firing operations

4. **Load Sample Rules** 📋
   - Automatically loads demo rules for customer classification
   - Includes VIP/Regular customer logic with discount rules

5. **Insert Customer Fact** ➕
   - Add sample customer data (Alice Johnson: 35yo, $120k income)
   - JSON fact format with automatic field mapping

6. **Fire Rules** 🔥
   - Execute all matching rules against inserted facts
   - Watch rules trigger based on customer criteria

7. **Execute Query** 🔍
   - Run queries to retrieve processed results
   - View JSON-formatted output with matched facts

### Keyboard Shortcuts

- **`Ctrl + Enter`**: Run complete automatic demo sequence

### Custom Operations

Use the manual input section for custom testing:

```json
// Custom Customer Fact
{
  "name": "Bob Smith",
  "age": 28,
  "income": 75000,
  "customerType": "",
  "discount": 0.0
}
```

## 🏗️ Architecture

```
wasm/
├── CMakeLists.txt          # Build configuration
├── src/
│   └── drills_wasm_bindings.cpp  # C++ WebAssembly interface
├── web/
│   ├── index.html          # Demo HTML interface
│   └── app.js              # JavaScript application logic
├── build/                  # Generated files
│   ├── drills.js           # WebAssembly script
│   └── drills.wasm         # Compiled WebAssembly module
└── README.md              # This file
```

### C++ Bindings

The WebAssembly module exports a `DrillsEngine` class with methods:

| Method | Description |
|--------|-------------|
| `init()` | Initialize the engine |
| `createKnowledgeBase(rules)` | Create knowledge base with DRL rules |
| `createSession()` | Start a new session |
| `insertFact(type, json)` | Insert JSON fact into session |
| `fireRules()` | Execute all valid rules |
| `query(name)` | Execute named query |
| `getStatus()` | Get engine status as JSON |

### Build Process

1. **Emscripten Compilation**
   - C++ source compiled to LLVM IR
   - LLVM IR converted to WebAssembly
   - JavaScript glue code generated

2. **Emscripten Flags**
   ```cmake
   -s WASM=1                          # Generate WebAssembly
   -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap']  # Export runtime methods
   -s EXPORTED_FUNCTIONS=['_malloc','_free']      # Export memory functions
   -s ALLOW_MEMORY_GROWTH=1           # Dynamic memory
   -s MODULARIZE=1                    # Module pattern
   -s EXPORT_NAME='DrillsEngine'      # Export name
   ```

3. **Generated Files**
   - `drills.js` - WebAssembly initialization and API
   - `drills.wasm` - Compiled WebAssembly binary

## 📝 Sample Rules

The demo includes business rules for customer classification:

```drl
rule "Customer Classification - VIP"
when
    $customer : Customer(age >= 30 && income > 100000)
then
    $customer.setCustomerType("VIP");
    rulesFired.add("VIP Customer Classification");
end

rule "Discount Calculation - VIP"
when
    $customer : Customer(customerType == "VIP")
then
    $customer.setDiscount(15.0);
    rulesFired.add("VIP Discount Applied");
end
```

## 🔧 Development

### Building for Development

```bash
# Debug build with verbose output
cd wasm/build
emcmake cmake .. -DCMAKE_BUILD_TYPE=Debug
emmake make VERBOSE=1

# Copy web files to build directory
cp ../web/* .
```

### Testing with Different Emscripten Versions

```bash
# Use specific Emscripten version
emsdk install 3.1.45
emsdk activate 3.1.45
source ./emsdk_env.sh

# Rebuild
emmake make clean
emmake make
```

### Memory Optimization

```cmake
# Add to CMakeLists.txt for smaller builds
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s MALLOC=emmalloc")
```

## 🚀 Deployment

### Static File Serving

Serve the `wasm/web` directory contents:
- `index.html` - Main demo page
- `app.js` - Application logic
- `drills.js` - WebAssembly loader
- `drills.wasm` - WebAssembly binary

### CDN Deployment

```html
<!-- Load from CDN -->
<script src="https://cdn.example.com/drills-drills.js"></script>
<script>
  const engine = new DrillsEngine();
</script>
```

### Security Considerations

- ✅ **Client-side Processing**: Rules execute in browser, data doesn't leave client
- ✅ **No Server Dependencies**: Purely static files
- ✅ **No External APIs**: Self-contained WebAssembly module
- ⚠️ **Code Obfuscation**: Consider minification for production

## 🔍 Performance

### Typical Metrics

| Operation | Time | Memory |
|-----------|------|--------|
| Module Load | 100-200ms | 5-10MB |
| Engine Init | 10-20ms | 2-5MB |
| Rule Processing | 5-50ms | 1-3MB |
| Query Execution | 2-10ms | 500KB-2MB |

### Optimization Tips

1. **Compilation Flags**
   ```cmake
   -O3                      # Maximum optimization
   -flto                    # Link-time optimization
   -s MALLOC=emmalloc      # Efficient memory allocation
   ```

2. **Memory Management**
   ```javascript
   // Monitor memory usage
   if ('memory' in performance) {
       console.log(`Memory: ${performance.memory.usedJSHeapSize / 1024 / 1024}MB`);
   }
   ```

3. **Lazy Loading**
   ```javascript
   // Load module on demand
   async function loadEngine() {
       const script = await import('./drills.js');
       return new script.DrillsEngine();
   }
   ```

## 🐛 Troubleshooting

### Common Issues

#### WebAssembly Load Failure
```
Error: Failed to load WebAssembly module
```
- ✅ Check network connectivity
- ✅ Verify Emscripten compilation
- ✅ Confirm `drills.wasm` file exists
- ✅ Check browser WebAssembly support

#### Module Initialization Error
```
Error: DrillsEngine is not defined
```
- ✅ Wait for module to load completely
- ✅ Check JavaScript console for errors
- ✅ Verify `drills.js` is loaded before use

#### Memory Allocation Error
```
Error: Cannot allocate WebAssembly memory
```
- ✅ Browser may be low on memory
- ✅ Close other tabs/applications
- ✅ Reduce data processing size

#### Rule Compilation Error
```
Error: Failed to load DRL rules
```
- ✅ Verify DRL syntax
- ✅ Check fact structure matches rules
- ✅ Ensure proper JSON formatting

### Browser Compatibility

| Browser | WebAssembly | Features | Testing |
|---------|-------------|----------|---------|
| Chrome 57+ | ✅ Full | All | 🧪 |
| Firefox 52+ | ✅ Full | All | 🧪 |
| Safari 12+ | ✅ Full | All | 🧪 |
| Edge 16+ | ✅ Full | All | 🧪 |
| Mobile Safari | ✅ Full | Most | 🧪 |
| Mobile Chrome | ✅ Full | All | 🧪 |

### Debug Mode

Enable debug logging:
```javascript
// In browser console
localStorage.setItem('drills-debug', 'true');

// Reload page and check console
```

## 📊 Use Cases

### Business Applications

- **Customer Segmentation**: Classify customers based on spending patterns
- **Risk Assessment**: Evaluate loan applications in real-time
- **Fraud Detection**: Apply rules to transaction data locally
- **Quality Control**: Validate product data against business rules

### Technical Applications

- **Data Validation**: Client-side form validation with custom rules
- **Offline Processing**: Process data without server connectivity
- **Privacy Compliance**: Keep sensitive data processing client-side
- **Performance**: Reduce server load through client-side processing

### Educational Applications

- **Rule Engine Learning**: Interactive rule testing environment
- **Algorithm Demonstration**: Visualize rule evaluation process
- **Prototyping**: Quick iteration of business rules

---

## 🤝 Contributing

1. Fork the repository
2. Create feature branch: `git checkout -b feature/new-rule-demo`
3. Test with multiple browsers
4. Submit pull request with documentation

## 📄 License

Apache License 2.0 - See [LICENSE](../LICENSE) file

## 🔗 Links

- [Drills Rule Engine](https://github.com/your-repo/drills) - Main project repository
- [Emscripten Documentation](https://emscripten.org/docs/) - Build system docs
- [WebAssembly MDN](https://developer.mozilla.org/en-US/docs/WebAssembly) - MDN documentation
- [Browser Compatibility](https://caniuse.com/wasm) - WebAssembly browser support

---

**Built with ❤️ using Emscripten & WebAssembly**
