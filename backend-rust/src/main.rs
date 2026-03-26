use gatherers_backend_rust::config::Config;

#[tokio::main]
async fn main() {
    let config = Config::from_env();
    eprintln!("gatherers-backend-rust placeholder listening config: {}", config.addr);
}
