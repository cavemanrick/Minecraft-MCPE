# Minecraft MCPE - EmuELEC S905 Mali Port

A port of the Minecraft Pi Edition to run on EmuELEC with Amlogic S905 (Mali-450MP GPU).

## Hardware Target

- **SoC**: Amlogic S905
- **GPU**: ARM Mali-450MP
- **Platform**: EmuELEC Linux
- **OpenGL ES**: 2.0 (full support)
- **Resolution**: 1280x720 (default, configurable)

## Features

✅ **Full OpenGL ES 2.0 Support**
- Optimized shaders for Mali-450MP
- Texture size limits respected (2048x2048 max)
- Fast fragment and vertex shader compilation

✅ **Complete Gamepad Support**
- XInput-style button mapping
- Left analog stick for movement (WASD keys)
- Right analog stick for camera control (mouse simulation)
- D-Pad for menu navigation
- All 10 standard buttons mapped

✅ **EGL Graphics Initialization**
- Standard EGL window surface creation
- Pbuffer fallback for headless environments
- Proper 60 FPS vsync handling

## Gamepad Button Mapping

| Button | Action |
|--------|--------|
| A (South) | Jump / Attack |
| B (East) | Sneak / Crouch |
| X (West) | Right Click / Use Item |
| Y (North) | Left Click / Break Block |
| LB | Inventory (E) |
| RB | Chat (T) |
| Back | Escape / Menu |
| Start | Pause |
| Left Stick Click | Sprint (CTRL) |
| Right Stick Click | Toggle Perspective (F) |
| Left Analog X | Strafe (A/D) |
| Left Analog Y | Forward/Back (W/S) |
| Right Analog X | Look Left/Right |
| Right Analog Y | Look Up/Down |
| LT / RT | Sprint Modifier |
| D-Pad | Arrow Keys (Menu Navigation) |

## Analog Stick Deadzone

- **Threshold**: 0.3 (30% of full range)
- Prevents stick drift from registering as input
- Configurable in `handleJoystickAxis()` function

## Building

### Prerequisites

```bash
# Install cross-compilation toolchain for ARM
sudo apt-get install build-essential gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf

# EmuELEC build dependencies
sudo apt-get install libpng-dev libsdl1.2-dev libglvnd-dev libegl-dev libgles2-mesa-dev
```

### Compilation

1. **For native S905 compilation (on device)**:
   ```bash
   cd handheld
   g++ -std=c++11 -O2 \
     -I./lib/include \
     -I./src \
     src/main_emuelec_s905.h \
     src/client/renderer/Shader.cpp \
     src/client/renderer/GLESLoader.cpp \
     -o minecraft_s905 \
     -lSDL -lpng -lEGL -lGLESv2 -lm
   ```

2. **For cross-compilation (from x86_64 to ARM)**:
   ```bash
   arm-linux-gnueabihf-g++ -std=c++11 -O2 \
     -I./lib/include \
     -I./src \
     -I/path/to/arm-sysroot/usr/include \
     src/main_emuelec_s905.h \
     src/client/renderer/Shader.cpp \
     src/client/renderer/GLESLoader.cpp \
     -L/path/to/arm-sysroot/usr/lib \
     -o minecraft_s905 \
     -lSDL -lpng -lEGL -lGLESv2 -lm
   ```

### Using CMake (Recommended)

Create a `CMakeLists.txt` in the `handheld/` directory:

```cmake
cmake_minimum_required(VERSION 3.10)
project(Minecraft_MCPE_S905)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "-O2 -march=armv7-a -mfpu=neon")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(minecraft_s905
    src/main_emuelec_s905.h
    src/client/renderer/Shader.cpp
    src/client/renderer/GLESLoader.cpp
    src/client/renderer/LevelRenderer.cpp
    # Add other source files as needed
)

target_link_libraries(minecraft_s905
    SDL
    png
    EGL
    GLESv2
    m
)
```

Then build:
```bash
mkdir build && cd build
cmake ..
make -j4
```

## Running on EmuELEC

1. **Copy executable to device**:
   ```bash
   scp minecraft_s905 root@<emuelec-ip>:/opt/minecraft/
   ```

2. **SSH into device and run**:
   ```bash
   ssh root@<emuelec-ip>
   cd /opt/minecraft
   ./minecraft_s905
   ```

3. **EmuELEC game launcher integration**:
   - Place executable in `/opt/roms/minecraft/`
   - Create a `.m3u` or launcher config pointing to it
   - Should appear in EmuELEC game list

## Shader Information

### default.vertex
- Standard vertex transformation
- Handles position, texture coordinates, and vertex coloring
- Calculates depth for fog effects

### default.fragment
- Texture sampling with color blending
- Fog rendering (3 modes: EXP, EXP2, LINEAR)
- Alpha testing for transparency
- Optimized for Mali-450MP precision (mediump float)

### S905 Mali-450MP Limits
- **Max texture size**: 2048x2048 pixels
- **Max varyings**: 8 (plenty for this shader)
- **Shader compilation**: Fast (~10-50ms)
- **Fragment shader precision**: mediump recommended

## Troubleshooting

### Shader Compilation Errors
Check the log output for `"DIAGNOSTIC: Shader compilation failed! Log:"`
- Ensure `#version 100` is in shader files for ES 2.0
- Verify precision qualifiers (`precision mediump float;`)

### Gamepad Not Detected
```bash
# Check SDL joystick detection
ls /dev/input/js*
# Or check EmuELEC gamepad mapping:
cat /etc/emuelec/configs/emulators/default.conf
```

### Texture Loading Issues
- Check that texture files exist in `handheld/data/images/`
- PNG format required (8-bit color + alpha)
- Textures > 2048x2048 will log a warning but still load

### EGL Surface Errors
- If you see "Failed to create EGL window surface", the device is using pbuffer mode
- This is normal for some EmuELEC configurations
- Performance will be slightly lower but should still work

### Low Performance
1. Reduce texture resolution
2. Lower polygon count in level rendering
3. Check if sprint (CTRL) is being held (can cause performance drops)
4. Monitor with: `glxinfo | grep "GLSL version"`

## Development Notes

### Key Files
- `handheld/src/main_emuelec_s905.h` - Main platform code + input handling
- `handheld/src/client/renderer/Shader.cpp` - Shader compilation
- `handheld/data/shaders/default.vertex` - Vertex shader
- `handheld/data/shaders/default.fragment` - Fragment shader (Mali-optimized)

### Extending Controls
To add custom button mappings, edit `handleJoystickButton()` in `main_emuelec_s905.h`:

```cpp
case 10:  // New button
    Keyboard::feed('Z', pressed);
    break;
```

### Performance Profiling
The code logs GPU info on startup:
```
OpenGL Vendor: ARM
OpenGL Renderer: Mali-450 MP
OpenGL Version: OpenGL ES 2.0
```

Use this to verify you're running on Mali-450.

## Known Limitations

- OpenGL ES 2.0 only (no ES 3.0 features)
- Texture size capped at 2048x2048
- No hardware-accelerated texture compression (ETC1/ETC2 available but not used)
- Single render target only
- No instancing support
- Limited to ~8 simultaneous lights per fragment

## License

Original Minecraft Pi Edition - Mojang Studios
Port modifications - EmuELEC S905 Mali GPU support

## Contributing

To contribute improvements:
1. Test on actual S905 Mali hardware
2. Profile performance with `glxinfo` and logcat
3. Submit PRs with detailed hardware test results

## Resources

- [Mali-450 OpenGL ES 2.0 Guide](https://developer.arm.com/documentation)
- [EmuELEC Documentation](https://emuelec.org/)
- [Amlogic S905 Specifications](https://www.amlogic.com/)
