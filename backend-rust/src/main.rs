use gatherers_backend_rust::{app, config::Config};

#[tokio::main]
async fn main() {
    let config = Config::from_env();
    app::serve(&config.addr)
        .await
        .expect("rust backend server should bind and run");
}
