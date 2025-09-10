# Building and Running Gatherers

This document contains comprehensive instructions for building and running the Gatherers simulation locally and for web deployment.

## Prerequisites

### Required Tools

1. **Install Rust**: Get the latest stable Rust from [rustup.rs](https://rustup.rs/)
   ```bash
   curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
   source ~/.cargo/env
   ```

2. **Install Trunk** (for web builds): 
   ```bash
   cargo install --locked trunk
   ```
   - The project works with Trunk 0.21.12+ (latest tested: 0.21.14)

3. **Add WebAssembly target** (for web builds):
   ```bash
   rustup target add wasm32-unknown-unknown
   ```

### System Requirements

- **Rust**: 1.70.0 or later (2021 edition)
- **Platform**: Windows, macOS, or Linux
- **Graphics**: OpenGL 3.3+ (native) or WebGL2 (web)
- **Memory**: 512MB+ available RAM

## Development Builds

### Native Desktop (Recommended for Development)

Run locally with Cargo for the best development experience:

```bash
cargo run
```

**Benefits:**
- Faster compilation
- Better performance
- Easier debugging with full Rust toolchain
- Hot reload with `cargo watch` if installed
- Direct access to system resources

**Development with auto-rebuild:**
```bash
# Install cargo-watch if not already installed
cargo install cargo-watch

# Auto-rebuild on file changes
cargo watch -x run
```

### Web Development

For web development with hot reload:

```bash
trunk serve
```

This will:
- Build the project for WebAssembly
- Start a local development server (default: http://127.0.0.1:8080)
- Watch for file changes and automatically rebuild/reload
- Serve the application with proper MIME types and headers

**Custom port:**
```bash
trunk serve --port 3000
```

**Open browser automatically:**
```bash
trunk serve --open
```

## Production Builds

### Web Deployment Build

To build optimized files for web deployment:

```bash
trunk build --release
```

This creates optimized files in the `dist/` directory:
- Applies aggressive size optimization
- Enables Link Time Optimization (LTO)
- Uses `wasm-opt` for further WebAssembly optimization
- Includes asset hashing for cache busting

**Build artifacts:**
- `dist/index.html` - Main HTML file
- `dist/an-gatherers-*.js` - JavaScript glue code
- `dist/an-gatherers-*.wasm` - WebAssembly binary
- `dist/index-*.css` - Compiled styles

### Native Release Build

For optimized native builds:

```bash
cargo build --release
```

The binary will be located at `target/release/an-gatherers` (or `.exe` on Windows).

## Configuration

### Simulation Parameters

Adjust simulation settings in `src/config.rs`:

```rust
impl Config {
    pub const ANT_SPEED: f32 = 1000.0;        // Movement speed
    pub const ANT_SPAWN_STEP: i32 = 50;       // Controls ant density
    pub const FOOD_COUNT: i32 = 80;           // Number of food items
    pub const PICKUP_COOLDOWN: f32 = 0.1;     // Seconds between actions
    pub const COLLISION_RADIUS: f32 = 10.0;   // Detection radius
    pub const SPATIAL_CELL_SIZE: f32 = 20.0;  // Performance tuning
}
```

### Build Configuration

#### Cargo Profiles

The project includes optimized build profiles in `Cargo.toml`:

- **Development**: Fast compilation with basic optimization for dependencies
- **Release**: Maximum optimization with size focus for web deployment

#### Trunk Configuration

Web-specific settings in `Trunk.toml`:
- Public URL path: `/gatherers/`
- Asset optimization: Enabled in release mode
- Output directory: `dist/`

## Deployment

### Static Web Hosting

The `dist/` folder after `trunk build --release` contains all files needed for static hosting:

1. **Manual deployment**: Upload `dist/` contents to your web server
2. **Automated deployment**: Use the provided script or CI/CD

### Example Deployment Script

The included `up.sh` shows Google Cloud Storage deployment:

```bash
#!/bin/bash
gsutil cp -r dist/* gs://an.org/gatherers/
```

**For other platforms:**

```bash
# Netlify
netlify deploy --prod --dir=dist

# GitHub Pages (using gh-pages branch)
git subtree push --prefix dist origin gh-pages

# AWS S3
aws s3 sync dist/ s3://your-bucket-name/ --delete
```

## Troubleshooting

### Common Build Issues

**"wasm32-unknown-unknown not found"**
```bash
rustup target add wasm32-unknown-unknown
```

**Trunk command not found:**
```bash
cargo install --locked trunk
# Ensure ~/.cargo/bin is in your PATH
```

**WebAssembly compilation errors:**
- Update Rust: `rustup update`
- Clean build: `cargo clean && trunk clean`
- Check Bevy version compatibility

### Performance Issues

**Slow compilation:**
- Use `cargo run` for development (much faster than web builds)
- Increase parallel jobs: `cargo build -j$(nproc)`
- Use `cargo check` for syntax validation without full build

**Runtime performance:**
- Native builds are significantly faster than WebAssembly
- For web builds, always use `--release` for deployment
- Adjust entity counts in `Config` if performance is poor

### Browser Compatibility

**WebGL2 Support Required:**
- Chrome 56+
- Firefox 51+
- Safari 15+
- Edge 79+

**Common browser issues:**
- Clear browser cache after updates
- Check browser console for WebAssembly errors
- Ensure hardware acceleration is enabled

### Development Tools

**Recommended VS Code Extensions:**
- `rust-analyzer` - Rust language support
- `CodeLLDB` - Debugging support
- `Trunk` - Trunk configuration support

**Useful Commands:**
```bash
# Check code without building
cargo check

# Run tests
cargo test

# Update dependencies
cargo update

# Clean build artifacts
cargo clean
trunk clean

# Check for security vulnerabilities
cargo audit
```

## Advanced Configuration

### Custom Bevy Features

The project uses minimal Bevy features for web compatibility. To add features, update `Cargo.toml`:

```toml
[dependencies.bevy]
features = [
  "bevy_sprite",
  "bevy_render", 
  "bevy_core_pipeline",
  "bevy_winit",
  "webgl2",
  "bevy_log",
  # Add new features here
]
```

### Environment Variables

**Development:**
```bash
# Enable debug logging
RUST_LOG=debug cargo run

# Show more compiler information  
RUSTC_LOG=debug trunk serve
```

**Build optimization:**
```bash
# Custom optimization level
CARGO_PROFILE_RELEASE_OPT_LEVEL=z trunk build --release
```

## Getting Help

- **Bevy Issues**: Check [Bevy's troubleshooting guide](https://bevy-cheatbook.github.io/pitfalls.html)
- **Trunk Issues**: See [Trunk documentation](https://trunkrs.dev/)
- **WebAssembly**: Consult [Rust and WebAssembly book](https://rustwasm.github.io/docs/book/)