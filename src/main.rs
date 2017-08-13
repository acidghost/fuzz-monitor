extern crate zmq;

use std::time::Instant;
use std::env::args;
use std::fmt;
use std::process::Command;

mod qemu;
mod perf;
mod myperf;


const ZMQ_BIND: &str = "tcp://*:5558";

trait AsMillis {
    fn as_millis(self) -> u64;
}

impl AsMillis for std::time::Duration {
    fn as_millis(self) -> u64 {
        self.as_secs() * 1000 + (self.subsec_nanos() as u64 / 1000000)
    }
}

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
    sut: Vec<String>,
    branch_store: myperf::BranchStore
}

impl fmt::Display for FuzzMonitor {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{} {}", self.tool, self.sut.join(" "))
    }
}

impl std::default::Default for FuzzMonitor {
    fn default() -> FuzzMonitor {
        FuzzMonitor {
            tool: MonitoringTool::Perf,
            sut: vec![],
            branch_store: myperf::BranchStore::empty()
        }
    }
}

impl FuzzMonitor {
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

    fn start(mut self) {
        assert!(!self.sut.is_empty());
        println!("[+] starting fuzz-monitor ({})", self);

        let context = zmq::Context::new();
        let receiver = context.socket(zmq::PULL).expect("failed to create zmq context");
        receiver.bind(ZMQ_BIND).expect(format!("failed to bind to {}", ZMQ_BIND).as_str());
        println!("[+] listening ({})...", ZMQ_BIND);

        let mut max_coverage = 0;
        let mut max_instant = Instant::now();
        let sut_clone = self.sut.clone();
        let sut_s = sut_clone.iter().map(|s| s.as_str()).collect::<Vec<&str>>();
        let sut = &sut_s.as_slice();

        let (sec_start, sec_end) = self.get_section_address(".text").unwrap();
        println!("[+] .text section bounds: 0x{:x} - 0x{:x}", sec_start, sec_end);

        loop {
            let bytes = receiver.recv_bytes(0).unwrap();
            let bytes_len = bytes.len();

            let (coverage, ms, new_branches) = self.trace(bytes, sut, sec_start, sec_end);

            let mut new_max = false;
            if coverage > max_coverage {
                max_coverage = coverage;
                max_instant = Instant::now();
                new_max = true;
            }

            print!("[{}] ", if new_max {'!'} else {'?'});
            if bytes_len > 1000 {
                print!("{:8.1}kb ", bytes_len as f64 / 1000.0);
            } else {
                print!("{:9}b ", bytes_len);
            }
            print!("{:8} ", coverage);
            if let Some(new_b) = new_branches {
                print!("{:6} ", new_b);
            }
            print!("{:8}ms {:8} max ", ms, max_coverage);
            let max_elapsed = max_instant.elapsed();
            if max_elapsed.as_secs() > 0 {
                println!("{:9}s ago", max_elapsed.as_secs());
            } else {
                println!("{:8}ms ago", max_elapsed.as_millis());
            }
        }
    }

    fn get_section_address(&self, section: &str) -> Option<(u64, u64)> {
        let sut = self.sut.get(0).unwrap();
        let out = Command::new("./section_address.sh").arg(sut).arg(section)
            .output().expect("failed to run section_address.sh");

        if !out.status.success() {
            return None;
        }

        let out_str = String::from_utf8_lossy(out.stdout.as_slice()).into_owned();
        let line = out_str.lines().nth(0).unwrap();
        let mut line_split = line.split(" ");
        let splitted_fst = line_split.nth(1).unwrap();
        let splitted_snd = line_split.nth(0).unwrap();
        Some((
            u64::from_str_radix(splitted_fst, 10).expect("failed parsing start address"),
            u64::from_str_radix(splitted_snd, 10).expect("failed parsing end address")
        ))
    }

    fn trace(&mut self, bytes: Vec<u8>, sut: &[&str], sec_start: u64, sec_end: u64)
        -> (u64, u64, Option<usize>) {
        let now = Instant::now();
        let mut new_branches = None;
        let coverage = match self.tool {
            MonitoringTool::Qemu => qemu::trace(bytes, sut).len() as u64,
            MonitoringTool::Perf => perf::trace(bytes, sut),
            MonitoringTool::CPerf => {
                // let mut bts_start: *mut BTSBranch = &mut BTSBranch { from: 0, to: 0, misc: 0 };
                // let mut count: u64 = 0;
                // let ret = myperf::trace(bytes, sut, &mut bts_start, &mut count).unwrap();
                // if ret == 0 {
                //     panic!("child ends");
                // } else {
                //     println!("[?] parent");
                // }
                // count
                let mut trace = myperf::trace2(bytes, sut);
                trace.retain(|bts|
                    (bts.from >= sec_start && bts.from <= sec_end) ||
                    (bts.to >= sec_start && bts.to <= sec_end));
                new_branches = Some(self.branch_store.add(trace.as_slice()));
                trace.len() as u64
            }
        };
        (coverage, now.elapsed().as_millis(), new_branches)
    }
}


fn main() {
    match FuzzMonitor::from_args() {
        Ok(fuzz_monitor) => fuzz_monitor.start(),
        Err(e) => println!("{}", e)
    }
}
