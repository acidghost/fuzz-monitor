use std::process::{Command, Stdio};
use std::io::{Read, Write};
use std::fs;
use std::u64;


const PERF: &str = "perf";
const PERF_OUT: &str = "./perf.txt";
const PERF_STAT: &str = "branches:u";
const PERF_ARGS: [&str; 6] = ["stat", "-x,", "-e", PERF_STAT, "-o", PERF_OUT];


pub fn trace(bytes: Vec<u8>, sut: &[&str]) -> u64 {
    let mut perf_proc = Command::new(PERF).args(&PERF_ARGS).args(sut)
        .stdin(Stdio::piped()).stdout(Stdio::null()).stderr(Stdio::null())
        .spawn().expect("failed to start perf");

    {
        let stdin = perf_proc.stdin.as_mut().expect("failed to get stdin");
        stdin.write_all(bytes.as_slice()).expect("failed to write to stdin");
    }

    perf_proc.wait().expect("failed to wait for perf");

    let mut file = fs::File::open(PERF_OUT).expect("failed to open perf output file");
    let mut perf_out_str = String::new();
    file.read_to_string(&mut perf_out_str).expect("failed to read perf output file");
    for line in perf_out_str.lines() {
        if line.contains(PERF_STAT) {
            let splitted_fst = line.split(",").next().unwrap();
            fs::remove_file(PERF_OUT).expect("unable to remove perf output file");
            return u64::from_str_radix(splitted_fst, 10).unwrap();
        }
    }

    panic!("failed to find instructions line in perf output")
}
