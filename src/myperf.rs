extern crate libc;

use std::ffi::CString;
use std::process::{Command, Stdio};
use std::io::Write;

use std::collections::HashMap;


#[repr(C)]
#[derive(Eq, PartialEq, Hash, Clone)]
pub struct BTSBranch {
    pub from: u64,
    pub to: u64,
    pub misc: u64
}

#[repr(C)]
#[allow(dead_code)]
enum llevel_t {
    MACHINE = 0,
    FATAL,
    ERROR,
    WARNING,
    INFO,
    DEBUG
}

#[link(name="perf", kind="static")]
extern "C" {
    static mut log_level: llevel_t;
    fn perf_monitor_api(data: *const u8, data_count: libc::size_t, argv: *const *const libc::c_char,
                        bts_start: *mut *mut BTSBranch, count: *mut u64) -> i32;
}


#[allow(dead_code)]
pub fn trace(bytes: Vec<u8>, argv: &[&str], bts_start: *mut *mut BTSBranch, count: *mut u64)
    -> Result<i32, i32> {
    let args: Vec<*const libc::c_char> = argv.iter().map(|arg|
        CString::new(arg.to_string()).unwrap().as_ptr()
    ).collect();

    let ret = unsafe {
        log_level = llevel_t::MACHINE;
        perf_monitor_api(bytes.as_slice().as_ptr(), bytes.len(), args.as_slice().as_ptr(), bts_start, count)
    };

    if ret < 0 { Err(ret) }
    else { Ok(ret) }
}

#[allow(dead_code)]
pub fn trace2(bytes: Vec<u8>, sut: &[&str]) -> Vec<BTSBranch> {
    let mut perf_proc = Command::new("./perf/perf").args(&["x"]).args(sut)
        .stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::null())
        .spawn().expect("failed to start perf");

    {
        let stdin = perf_proc.stdin.as_mut().expect("failed to get stdin");
        stdin.write_all(bytes.as_slice()).expect("failed to write to stdin");
    }

    let perf_out = perf_proc.wait_with_output().expect("failed to wait for perf");
    let mut branches: Vec<BTSBranch> = vec![];
    let perf_out_str = String::from_utf8_lossy(perf_out.stdout.as_slice()).into_owned();

    for line in perf_out_str.lines() {
        if line.starts_with("branch") {
            let mut splitted = line.split(",");
            let splitted_fst = splitted.nth(1).expect("no second element");
            let splitted_snd = splitted.nth(0).expect("no third element");
            branches.push(BTSBranch {
                from: u64::from_str_radix(splitted_fst, 10).expect("failed parsing from"),
                to: u64::from_str_radix(splitted_snd, 10).expect("failed parsing to"),
                misc: 0
            });
        }
    }

    branches
}


pub struct BranchStore {
    hits: HashMap<BTSBranch, u64>
}

impl BranchStore {
    pub fn empty() -> BranchStore {
        BranchStore { hits: HashMap::new() }
    }

    pub fn add(&mut self, branches: &[BTSBranch]) -> usize {
        let mut new = 0;
        for branch in branches {
            if let Some(entry) = self.hits.get_mut(branch) {
                *entry += 1;
                continue;
            }
            self.hits.insert(branch.clone(), 1);
            new += 1;
        }
        new
    }
}
