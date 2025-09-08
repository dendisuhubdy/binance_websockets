# Create build directory
mkdir build && cd build

# Configure (Release build)
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build . --parallel

# Run
./binance_orderbook
