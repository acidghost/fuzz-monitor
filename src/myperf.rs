extern crate libc;

use std::ffi::CString;


#[repr(C)]
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


pub fn monitor_api(bytes: Vec<u8>, argv: &[&str], bts_start: *mut *mut BTSBranch, count: *mut u64)
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
