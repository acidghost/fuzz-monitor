use std::process::{Command, Stdio};
use std::io::Write;
use std::u64;
use std::fs;


const QEMU: &str = "./qemu-2.9.0/x86_64-linux-user/qemu-x86_64";
const QEMU_ARGS: [&str; 2] = ["-trace", "events=./events"];

const QEMU_TRACE_PY: &str = "./qemu-2.9.0/scripts/simpletrace.py";
const QEMU_TRACE_ARG: &str = "./qemu-2.9.0/trace-events-all";


pub fn trace(bytes: Vec<u8>, sut: &[&str]) -> Vec<u64> {
    let mut qemu_proc = Command::new(QEMU).args(&QEMU_ARGS).args(sut)
        .stdin(Stdio::piped()).stdout(Stdio::null()).stderr(Stdio::null())
        .spawn().expect("failed to start qemu");

    {
        let stdin = qemu_proc.stdin.as_mut().expect("failed to get stdin");
        stdin.write_all(bytes.as_slice()).expect("failed to write to stdin");
    }

    qemu_proc.wait().expect("failed to wait for qemu");
    let qemu_pid = qemu_proc.id();

    let filename = format!("./trace-{}", qemu_pid);
    let lines = parse_trace(&filename);
    fs::remove_file(&filename).expect(format!("unable to remove {:?}", filename).as_str());
    lines
}

fn parse_trace(trace_filename: &String) -> Vec<u64> {
    let trace_out = Command::new(QEMU_TRACE_PY).arg(QEMU_TRACE_ARG).arg(trace_filename)
        .output().expect("failed to run trace parser");
    let trace_out_str = String::from_utf8(trace_out.stdout).unwrap();

    let mut trace_lines: Vec<u64> = Vec::new();
    for line in trace_out_str.lines() {
        for part in line.split(" ") {
            if part.starts_with("pc=0x") {
                trace_lines.push(u64::from_str_radix(&part[5..], 16).unwrap())
            }
        }
    }

    trace_lines
}
