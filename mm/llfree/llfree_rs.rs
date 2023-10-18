#![no_std]
#![feature(int_roundings)]
#![feature(alloc_error_handler)]

#[allow(unused_imports)]
#[macro_use]
extern crate alloc;

use core::alloc::{GlobalAlloc, Layout};
use core::ffi::{c_char, c_void};
use core::fmt::{self, Write};
use core::panic::PanicInfo;
use core::sync::atomic::{self, AtomicU64, Ordering};

use alloc::boxed::Box;
use log::{error, warn, Level, Metadata, Record};

use llfree::frame::{Frame, PFNRange, PFN};
use llfree::Alloc;
use llfree::Error;
use llfree::Init::{Recover, Volatile};
use llfree::LLFree;

const MOD: &[u8] = b"llfree\0";

extern "C" {
    /// Linux provided alloc function
    fn llfree_linux_alloc(node: u64, size: u64, align: u64) -> *mut u8;
    /// Linux provided free function
    fn llfree_linux_free(ptr: *mut u8, size: u64, align: u64);
    /// Linux provided printk function
    fn llfree_linux_printk(format: *const u8, module_name: *const u8, args: *const c_void);
}

static NODE_ID: AtomicU64 = AtomicU64::new(0);

/// Initialize the allocator for the given memory range.
/// If `overwrite` is nonzero no existing allocator state is recovered.
/// Returns 0 on success or an error code.
#[cold]
#[link_section = ".init.text"]
#[no_mangle]
pub extern "C" fn llfree_init(
    node: u64,
    cores: u32,
    persistent: u8,
    start: *mut c_void,
    pages: u64,
) -> *mut c_void {
    init_logging();

    warn!("Initializing inside rust");

    let init = if persistent == 0 { Volatile } else { Recover };
    // Linux usually initializes its allocator with all memory occupied and afterwards frees the avaliable memory of the boot allocator.
    // If persistent, we have to store our own metadata into the zone, thus require the memory to be avaliable.
    let free_all = persistent != 0;
    assert!(start as usize % Frame::SIZE == 0, "Invalid alignment");

    // Set zone id for allocations
    NODE_ID.store(node, Ordering::Release);

    if pages > 0 {
        let pfn = PFN::from_ptr(start.cast());
        let area = pfn..pfn.off(pages as _);

        match LLFree::new(cores as _, area.clone(), init, free_all) {
            Ok(alloc) => {
                warn!("setup mem={:?} ({})", area.as_ptr_range(), alloc.frames());
                // Move to newly allocated memory and leak address
                Box::leak(Box::new(alloc)) as *mut LLFree as _
            }
            Err(e) => e as usize as _,
        }
    } else {
        Error::Initialization as usize as _
    }
}

/// Shut down the allocator normally.
#[cold]
#[no_mangle]
pub extern "C" fn llfree_uninit(alloc: *mut LLFree) {
    if !alloc.is_null() {
        unsafe { core::mem::drop(Box::from_raw(alloc as *mut LLFree)) };
    }
}

/// Allocates 2^order pages. Returns >=PAGE_SIZE on success an error code.
#[no_mangle]
pub extern "C" fn llfree_get(alloc: *const LLFree, core: u32, order: u32) -> *mut u8 {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        match alloc.get(core as _, order as _) {
            Ok(addr) => addr.as_ptr_mut().cast(),
            Err(e) => e as u64 as _,
        }
    } else {
        Error::Initialization as u64 as _
    }
}

/// Frees a previously allocated page. Returns 0 on success or an error code.
#[no_mangle]
pub extern "C" fn llfree_put(alloc: *const LLFree, core: u32, addr: *mut u8, order: u32) -> u64 {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        match alloc.put(core as _, PFN::from_ptr(addr.cast()), order as _) {
            Ok(_) => 0,
            Err(e) => e as u64,
        }
    } else {
        Error::Initialization as u64
    }
}

#[no_mangle]
pub extern "C" fn llfree_drain(alloc: *const LLFree, core: u32) -> u64 {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        match alloc.drain(core as _) {
            Ok(_) => 0,
            Err(e) => e as u64,
        }
    } else {
        Error::Initialization as u64
    }
}

#[no_mangle]
pub extern "C" fn llfree_is_free(alloc: *const LLFree, addr: *mut u8, order: u32) -> u64 {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        alloc.is_free(PFN::from_ptr(addr.cast()), order as _) as _
    } else {
        0
    }
}

#[cold]
#[no_mangle]
pub extern "C" fn llfree_free_count(alloc: *const LLFree) -> u64 {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        alloc.free_frames() as u64
    } else {
        0
    }
}

#[cold]
#[no_mangle]
pub extern "C" fn llfree_free_huge_count(alloc: *const LLFree) -> u64 {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        alloc.free_huge_frames() as u64
    } else {
        0
    }
}

#[cold]
#[no_mangle]
pub extern "C" fn llfree_printk(alloc: *const LLFree) {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        warn!("{alloc:?}");
    }
}

#[cold]
#[no_mangle]
pub extern "C" fn llfree_for_each_huge_page(
    alloc: *const LLFree,
    f: extern "C" fn(*mut c_void, u16),
    arg: *mut c_void,
) {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        alloc.for_each_huge_frame(|_, free| f(arg, free as _))
    }
}

/// # Safety
/// This writes into the provided memory buffer which has to be valid.
#[cold]
#[no_mangle]
pub extern "C" fn llfree_dump(alloc: *const LLFree, buf: *mut u8, len: u64) -> u64 {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        let mut writer =
            unsafe { RawFormatter::from_ptrs(buf, (buf as usize).saturating_add(len as _) as _) };
        if writeln!(writer, "{alloc:?}").is_err() {
            error!("write failed after {}B", writer.bytes_written());
        }
        writer.bytes_written() as _
    } else {
        0
    }
}

struct LinuxAlloc;
unsafe impl GlobalAlloc for LinuxAlloc {
    #[cold]
    unsafe fn alloc(&self, layout: core::alloc::Layout) -> *mut u8 {
        llfree_linux_alloc(
            NODE_ID.load(Ordering::Acquire),
            layout.size() as _,
            layout.align() as _,
        )
    }

    #[cold]
    unsafe fn dealloc(&self, ptr: *mut u8, layout: core::alloc::Layout) {
        llfree_linux_free(ptr, layout.size() as _, layout.align() as _);
    }
}

#[global_allocator]
static LINUX_ALLOC: LinuxAlloc = LinuxAlloc;

#[alloc_error_handler]
pub fn on_oom(layout: Layout) -> ! {
    error!("Unable to allocate {} bytes", layout.size());
    loop {
        atomic::compiler_fence(Ordering::SeqCst);
    }
}

#[inline(never)]
#[panic_handler]
pub fn panic(info: &PanicInfo) -> ! {
    error!("{info}");
    loop {
        atomic::compiler_fence(Ordering::SeqCst);
    }
}

/// Printing facilities.
///
/// C header: [`include/linux/printk.h`](../../../../include/linux/printk.h)
///
/// Reference: <https://www.kernel.org/doc/html/latest/core-api/printk-basics.html>
struct PrintKLogger;

const fn max_log_level() -> Level {
    cfg_if::cfg_if! {
        if #[cfg(feature = "max_level_debug")] {
            Level::Debug
        } else if #[cfg(feature = "max_level_info")] {
            Level::Info
        } else if #[cfg(feature = "max_level_error")] {
            Level::Error
        } else {
            Level::Warn
        }
    }
}

impl log::Log for PrintKLogger {
    fn enabled(&self, metadata: &Metadata) -> bool {
        metadata.level() <= max_log_level()
    }

    fn log(&self, record: &Record) {
        if self.enabled(record.metadata()) {
            match record.metadata().level() {
                Level::Error => unsafe { call_printk(&format_strings::ERR, record) },
                Level::Warn => unsafe { call_printk(&format_strings::WARNING, record) },
                Level::Info => unsafe { call_printk(&format_strings::INFO, record) },
                Level::Debug => unsafe { call_printk(&format_strings::DEBUG, record) },
                Level::Trace => unsafe { call_printk(&format_strings::DEBUG, record) },
            }
        }
    }

    fn flush(&self) {}
}

static LOGGER: PrintKLogger = PrintKLogger;

pub fn init_logging() {
    let _ignored =
        log::set_logger(&LOGGER).map(|()| log::set_max_level(max_log_level().to_level_filter()));
}

// Called from `vsprintf` with format specifier `%pA`.
#[no_mangle]
pub extern "C" fn rust_fmt_argument(
    buf: *mut c_char,
    end: *mut c_char,
    ptr: *const c_void,
) -> *mut c_char {
    // SAFETY: The C contract guarantees that `buf` is valid if it's less than `end`.
    let mut w = unsafe { RawFormatter::from_ptrs(buf.cast(), end.cast()) };
    let record = unsafe { &*(ptr as *const Record) };
    let _ = w.write_fmt(format_args!(
        "{}:{} ",
        record
            .file()
            .map(|f| f.rsplit_once('/').map(|f| f.1).unwrap_or(f))
            .unwrap_or_default(),
        record.line().unwrap_or_default()
    ));
    let _ = w.write_fmt(*record.args());
    w.pos().cast()
}

/// Format strings.
///
/// Public but hidden since it should only be used from public macros.
#[doc(hidden)]
pub mod format_strings {
    // Linux bindings
    mod bindings {
        pub const KERN_EMERG: &[u8; 3] = b"\x010\0";
        pub const KERN_ALERT: &[u8; 3] = b"\x011\0";
        pub const KERN_CRIT: &[u8; 3] = b"\x012\0";
        pub const KERN_ERR: &[u8; 3] = b"\x013\0";
        pub const KERN_WARNING: &[u8; 3] = b"\x014\0";
        pub const KERN_NOTICE: &[u8; 3] = b"\x015\0";
        pub const KERN_INFO: &[u8; 3] = b"\x016\0";
        pub const KERN_DEBUG: &[u8; 3] = b"\x017\0";
    }

    // Generate the format strings at compile-time.
    //
    // This avoids the compiler generating the contents on the fly in the stack.
    //
    // Furthermore, `static` instead of `const` is used to share the strings
    // for all the kernel.
    pub static EMERG: [u8; LENGTH] = generate(bindings::KERN_EMERG);
    pub static ALERT: [u8; LENGTH] = generate(bindings::KERN_ALERT);
    pub static CRIT: [u8; LENGTH] = generate(bindings::KERN_CRIT);
    pub static ERR: [u8; LENGTH] = generate(bindings::KERN_ERR);
    pub static WARNING: [u8; LENGTH] = generate(bindings::KERN_WARNING);
    pub static NOTICE: [u8; LENGTH] = generate(bindings::KERN_NOTICE);
    pub static INFO: [u8; LENGTH] = generate(bindings::KERN_INFO);
    pub static DEBUG: [u8; LENGTH] = generate(bindings::KERN_DEBUG);

    /// The length we copy from the `KERN_*` kernel prefixes.
    const LENGTH_PREFIX: usize = 2;

    /// The length of the fixed format strings.
    pub const LENGTH: usize = 10;

    /// Generates a fixed format string for the kernel's [`_printk`].
    ///
    /// The format string is always the same for a given level, i.e. for a
    /// given `prefix`, which are the kernel's `KERN_*` constants.
    ///
    /// [`_printk`]: ../../../../include/linux/printk.h
    const fn generate(prefix: &[u8; 3]) -> [u8; LENGTH] {
        // Ensure the `KERN_*` macros are what we expect.
        assert!(prefix[0] == b'\x01');
        assert!(prefix[1] >= b'0' && prefix[1] <= b'7');
        assert!(prefix[2] == b'\x00');

        let suffix: &[u8; LENGTH - LENGTH_PREFIX] = b"%s: %pA\0";
        [
            prefix[0], prefix[1], suffix[0], suffix[1], suffix[2], suffix[3], suffix[4], suffix[5],
            suffix[6], suffix[7],
        ]
    }
}

/// Prints a message via the kernel's [`_printk`].
///
/// Public but hidden since it should only be used from public macros.
///
/// # Safety
///
/// The format string must be one of the ones in [`format_strings`], and
/// the module name must be null-terminated.
///
/// [`_printk`]: ../../../../include/linux/_printk.h
#[doc(hidden)]
pub unsafe fn call_printk(format_string: &[u8; format_strings::LENGTH], args: &Record) {
    // `_printk` does not seem to fail in any path.
    llfree_linux_printk(
        format_string.as_ptr() as _,
        MOD.as_ptr(),
        args as *const _ as *const c_void,
    );
}

/// Allows formatting of [`fmt::Arguments`] into a raw buffer.
///
/// It does not fail if callers write past the end of the buffer so that they can calculate the
/// size required to fit everything.
///
/// # Invariants
///
/// The memory region between `pos` (inclusive) and `end` (exclusive) is valid for writes if `pos`
/// is less than `end`.
struct RawFormatter {
    // Use `usize` to use `saturating_*` functions.
    #[allow(dead_code)]
    beg: usize,
    pos: usize,
    end: usize,
}

impl RawFormatter {
    /// Creates a new instance of [`RawFormatter`] with the given buffer pointers.
    ///
    /// # Safety
    ///
    /// If `pos` is less than `end`, then the region between `pos` (inclusive) and `end`
    /// (exclusive) must be valid for writes for the lifetime of the returned [`RawFormatter`].
    unsafe fn from_ptrs(pos: *mut u8, end: *mut u8) -> Self {
        // INVARIANT: The safety requierments guarantee the type invariants.
        Self {
            beg: pos as _,
            pos: pos as _,
            end: end as _,
        }
    }

    /// Returns the current insert position.
    ///
    /// N.B. It may point to invalid memory.
    fn pos(&self) -> *mut u8 {
        self.pos as _
    }

    /// Return the number of bytes written to the formatter.
    fn bytes_written(&self) -> usize {
        self.pos - self.beg
    }
}

impl fmt::Write for RawFormatter {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        // `pos` value after writing `len` bytes. This does not have to be bounded by `end`, but we
        // don't want it to wrap around to 0.
        let pos_new = self.pos.saturating_add(s.len());

        // Amount that we can copy. `saturating_sub` ensures we get 0 if `pos` goes past `end`.
        let len_to_copy = core::cmp::min(pos_new, self.end).saturating_sub(self.pos);

        if len_to_copy > 0 {
            // SAFETY: If `len_to_copy` is non-zero, then we know `pos` has not gone past `end`
            // yet, so it is valid for write per the type invariants.
            unsafe {
                core::ptr::copy_nonoverlapping(
                    s.as_bytes().as_ptr(),
                    self.pos as *mut u8,
                    len_to_copy,
                )
            };
        }

        self.pos = pos_new;
        Ok(())
    }
}
