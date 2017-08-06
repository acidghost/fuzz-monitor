extern crate zmq;

use std::process::{Command, Stdio};
use std::fs;
use std::io::Write;
use std::u64;


const QEMU: &str = "./qemu-2.9.0/x86_64-linux-user/qemu-x86_64";
const SUT: &str = "../LAVA-M/bin/base64";
const QEMU_ARGS: [&str; 4] = ["-trace", "events=./events", SUT, "-d"];

const QEMU_TRACE_PY: &str = "./qemu-2.9.0/scripts/simpletrace.py";
const QEMU_TRACE_ARG: &str = "./qemu-2.9.0/trace-events-all";

type TraceLine = u64;

fn trace(bytes: Vec<u8>) -> u32 {
    let mut qemu_proc = Command::new(QEMU).args(&QEMU_ARGS)
        .stdin(Stdio::piped()).stdout(Stdio::null()).stderr(Stdio::null())
        .spawn().expect("failed to start qemu");

    {
        let stdin = qemu_proc.stdin.as_mut().expect("failed to get stdin");
        stdin.write_all(bytes.as_slice()).expect("failed to write to stdin");
    }

    qemu_proc.wait().expect("failed to wait for qemu");
    qemu_proc.id()
}

fn parse_trace(trace_filename: &String) -> Vec<TraceLine> {
    let trace_out = Command::new(QEMU_TRACE_PY).arg(QEMU_TRACE_ARG).arg(trace_filename)
        .output().expect("failed to run trace parser");
    let trace_out_str = String::from_utf8(trace_out.stdout).unwrap();

    let mut trace_lines: Vec<TraceLine> = Vec::new();
    for line in trace_out_str.lines() {
        for part in line.split(" ") {
            if part.starts_with("pc=0x") {
                trace_lines.push(u64::from_str_radix(&part[5..], 16).unwrap())
            }
        }
    }

    trace_lines
}

fn main() {
    println!("[+] starting fuzz-monitor");
    let context = zmq::Context::new();
    let receiver = context.socket(zmq::PULL).unwrap();
    assert!(receiver.bind("tcp://*:5558").is_ok());
    println!("[+] listening...");

    let mut max_count = 0;

    loop {
        let bytes = receiver.recv_bytes(0).unwrap();
        let qemu_pid = trace(bytes);

        let filename = format!("./trace-{}", qemu_pid);
        let trace_lines = parse_trace(&filename);
        // TODO: do something useful with trace lines...
        let count = trace_lines.len();
        let mut new_max = false;
        if count > max_count {
            max_count = count;
            new_max = true;
        }
        println!("[{}] computed {:?} (max {:?})", if new_max {'!'} else {'?'}, count, max_count);

        fs::remove_file(&filename).expect(format!("unable to remove {:?}", filename).as_str());
    }
}
