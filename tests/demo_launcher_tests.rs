use std::{
    fs,
    net::TcpListener,
    path::{Path, PathBuf},
    process::{Child, Command, Stdio},
    thread,
    time::{Duration, Instant},
};

#[test]
fn demo_launcher_interrupt_stops_stubbed_backend_and_sims() {
    let repo_root = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let backend_port = free_local_port();
    let temp_dir = std::env::temp_dir().join(format!(
        "gatherers-demo-launcher-test-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .expect("unix epoch")
            .as_millis()
    ));
    fs::create_dir_all(&temp_dir).expect("temp dir");
    let stdout_log = temp_dir.join("launcher.stdout.log");
    let stderr_log = temp_dir.join("launcher.stderr.log");

    let mut launcher = Command::new("bash");
    launcher
        .arg("./scripts/run_rust_backend_demo.sh")
        .arg("--count")
        .arg("2")
        .arg("--backend-addr")
        .arg(format!("127.0.0.1:{backend_port}"))
        .current_dir(&repo_root)
        .env("GATHERERS_DEMO_SKIP_BUILD", "1")
        .env(
            "GATHERERS_DEMO_BACKEND_BIN",
            repo_root.join("tests/fixtures/demo_stub_backend.sh"),
        )
        .env(
            "GATHERERS_DEMO_SIM_BIN",
            repo_root.join("tests/fixtures/demo_stub_sim.sh"),
        )
        .env("GATHERERS_DEMO_LOG_DIR", &temp_dir)
        .stdout(
            fs::File::create(&stdout_log)
                .expect("launcher stdout log should be creatable"),
        )
        .stderr(
            fs::File::create(&stderr_log)
                .expect("launcher stderr log should be creatable"),
        );

    let mut child = launcher.spawn().expect("launcher should start");

    let backend_pid_file = temp_dir.join("backend.pid");
    let sim_one_pid_file = temp_dir.join("sim-demo-01.pid");
    let sim_two_pid_file = temp_dir.join("sim-demo-02.pid");
    wait_for_file(&backend_pid_file, &temp_dir);
    wait_for_file(&sim_one_pid_file, &temp_dir);
    wait_for_file(&sim_two_pid_file, &temp_dir);

    let backend_pid = read_pid(&backend_pid_file);
    let sim_one_pid = read_pid(&sim_one_pid_file);
    let sim_two_pid = read_pid(&sim_two_pid_file);

    assert!(is_alive(backend_pid), "backend stub should be alive before interrupt");
    assert!(is_alive(sim_one_pid), "sim one stub should be alive before interrupt");
    assert!(is_alive(sim_two_pid), "sim two stub should be alive before interrupt");

    interrupt(&mut child);
    wait_for_exit(&mut child);

    wait_for_not_alive(backend_pid);
    wait_for_not_alive(sim_one_pid);
    wait_for_not_alive(sim_two_pid);

    assert!(
        !is_alive(backend_pid),
        "backend stub should stop after launcher interrupt"
    );
    assert!(
        !is_alive(sim_one_pid),
        "sim one stub should stop after launcher interrupt"
    );
    assert!(
        !is_alive(sim_two_pid),
        "sim two stub should stop after launcher interrupt"
    );
}

fn wait_for_file(path: &Path, temp_dir: &Path) {
    let deadline = Instant::now() + Duration::from_secs(10);
    while Instant::now() < deadline {
        if path.exists() {
            return;
        }
        thread::sleep(Duration::from_millis(100));
    }
    let stdout = fs::read_to_string(temp_dir.join("launcher.stdout.log")).unwrap_or_default();
    let stderr = fs::read_to_string(temp_dir.join("launcher.stderr.log")).unwrap_or_default();
    panic!(
        "timed out waiting for file {}\nstdout:\n{}\nstderr:\n{}",
        path.display(),
        stdout,
        stderr
    );
}

fn read_pid(path: &Path) -> u32 {
    fs::read_to_string(path)
        .expect("pid file should be readable")
        .trim()
        .parse()
        .expect("pid file should contain a valid pid")
}

fn interrupt(child: &mut Child) {
    let status = Command::new("kill")
        .arg("-INT")
        .arg(child.id().to_string())
        .status()
        .expect("kill command should run");
    assert!(status.success(), "kill -INT should succeed");
}

fn wait_for_exit(child: &mut Child) {
    let deadline = Instant::now() + Duration::from_secs(10);
    while Instant::now() < deadline {
        if child.try_wait().expect("try_wait should succeed").is_some() {
            return;
        }
        thread::sleep(Duration::from_millis(100));
    }
    let _ = child.kill();
    panic!("launcher did not exit after interrupt");
}

fn wait_for_not_alive(pid: u32) {
    let deadline = Instant::now() + Duration::from_secs(10);
    while Instant::now() < deadline {
        if !is_alive(pid) {
            return;
        }
        thread::sleep(Duration::from_millis(100));
    }
}

fn is_alive(pid: u32) -> bool {
    Command::new("kill")
        .arg("-0")
        .arg(pid.to_string())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .map(|status| status.success())
        .unwrap_or(false)
}

fn free_local_port() -> u16 {
    let listener = TcpListener::bind("127.0.0.1:0").expect("should bind an ephemeral port");
    listener
        .local_addr()
        .expect("listener should expose local addr")
        .port()
}
