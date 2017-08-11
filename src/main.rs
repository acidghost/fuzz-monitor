extern crate zmq;

use std::time::Instant;
use std::env::args;
use std::fmt;

mod qemu;
mod perf;
mod myperf;


enum MonitoringTool { Perf, Qemu, CPerf }

impl fmt::Display for MonitoringTool {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            &MonitoringTool::Qemu => write!(f, "QEMU"),
            &MonitoringTool::Perf => write!(f, "PERF"),
            &MonitoringTool::CPerf => write!(f, "CPERF")
        }
    }
}

struct FuzzMonitor {
    tool: MonitoringTool,
    sut: Vec<String>
}

impl fmt::Display for FuzzMonitor {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{} {}", self.tool, self.sut.join(" "))
    }
}

impl FuzzMonitor {
    fn default() -> FuzzMonitor {
        FuzzMonitor { tool: MonitoringTool::Perf, sut: vec![] }
    }

    fn from_args<'a>() -> Result<FuzzMonitor, &'a str> {
        let mut options = FuzzMonitor::default();
        let mut do_sut = false;
        for arg in args() {
            match arg.as_str() {
                x if do_sut == true => options.sut.push(x.to_string()),
                "-p" => options.tool = MonitoringTool::Perf,
                "-q" => options.tool = MonitoringTool::Qemu,
                "-c" => options.tool = MonitoringTool::CPerf,
                "--" => do_sut = true,
                _ => ()
            }
        }
        if options.sut.is_empty() {
            Err("usage: fuzz-monitor [-p|-q|-c] -- command [args]")
        } else {
            Ok(options)
        }
    }

    fn start(self) {
        assert!(!self.sut.is_empty());
        println!("[+] starting fuzz-monitor ({})", self);

        let context = zmq::Context::new();
        let receiver = context.socket(zmq::PULL).expect("failed to create zmq context");
        receiver.bind("tcp://*:5558").expect("failed to bind to tcp://*:5558");
        println!("[+] listening...");

        let mut max_count = 0;
        let sut_s = self.sut.iter().map(|s| s.as_str()).collect::<Vec<&str>>();
        let sut = &sut_s.as_slice();

        loop {
            let bytes = receiver.recv_bytes(0).unwrap();

            let now = Instant::now();
            let count = match self.tool {
                MonitoringTool::Qemu => qemu::trace(bytes, sut).len() as u64,
                MonitoringTool::Perf => perf::trace(bytes, sut),
                MonitoringTool::CPerf => {
                    let mut bts_start: *mut myperf::BTSBranch = &mut myperf::BTSBranch { from: 0, to: 0, misc: 0 };
                    let mut count: u64 = 0;
                    myperf::monitor_api(bytes, sut, &mut bts_start, &mut count).unwrap();
                    count
                }
            };
            let elapsed = now.elapsed();

            let mut new_max = false;
            if count > max_count {
                max_count = count;
                new_max = true;
            }

            let ms = elapsed.as_secs() * 1000 + (elapsed.subsec_nanos() as u64 / 1000000);
            println!("[{}] {} (max {}, in: {}ms)", if new_max {'!'} else {'?'}, count, max_count, ms);
        }
    }
}


fn main() {
    match FuzzMonitor::from_args() {
        Ok(fuzz_monitor) => fuzz_monitor.start(),
        Err(e) => println!("{}", e)
    }
}
