#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Config {
    pub addr: String,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            addr: "127.0.0.1:8080".to_string(),
        }
    }
}

impl Config {
    pub fn from_env() -> Self {
        Self {
            addr: std::env::var("GATHERERS_BACKEND_RUST_ADDR")
                .unwrap_or_else(|_| Self::default().addr),
        }
    }
}
