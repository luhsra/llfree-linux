#![no_std]
#![feature(int_roundings)]
#![feature(alloc_error_handler)]
#![feature(c_size_t)]

use core::ffi::{c_char, c_size_t, c_void};
use core::fmt::{self, Write};
use core::mem::{align_of, size_of};
use core::panic::PanicInfo;
use core::ptr::null_mut;
use core::slice;

use log::{error, warn, Level, Metadata, Record};

use llfree::util::{align_down, Align};
use llfree::{Alloc, Error, Init::AllocAll, LLFree as LLFreeRaw, MetaSize, Result};

const MOD: &[u8] = b"llfree\0";

extern "C" {
    /// Linux provided alloc function
    fn llfree_linux_alloc(node: c_size_t, size: c_size_t, align: c_size_t) -> *mut u8;
    /// Linux provided free function
    fn llfree_linux_free(ptr: *mut u8, size: c_size_t, align: c_size_t);
    /// Linux provided printk function
    fn llfree_linux_printk(format: *const u8, module_name: *const u8, args: *const c_void);
    /// Trigger a kernel panic
    fn llfree_panic() -> !;
}

type LLFree = LLFreeRaw<'static>;

#[repr(C)]
#[allow(non_camel_case_types)]
pub struct result_t {
    pub val: i64,
}
impl From<Error> for result_t {
    fn from(value: Error) -> Self {
        match value {
            Error::Memory => Self { val: -1 },
            Error::Retry => Self { val: -2 },
            Error::Address => Self { val: -3 },
            Error::Initialization => Self { val: -4 },
        }
    }
}
impl From<Result<()>> for result_t {
    fn from(value: Result<()>) -> Self {
        match value {
            Ok(_) => result_t { val: 0 },
            Err(e) => e.into(),
        }
    }
}
impl From<Result<usize>> for result_t {
    fn from(value: Result<usize>) -> Self {
        match value {
            Ok(v) => result_t { val: v as _ },
            Err(e) => e.into(),
        }
    }
}

/// Initialize the allocator for the given memory range.
/// If `overwrite` is nonzero no existing allocator state is recovered.
/// Returns 0 on success or an error code.
#[cold]
#[link_section = ".init.text"]
#[no_mangle]
pub extern "C" fn llfree_node_init(
    node: c_size_t,
    cores: c_size_t,
    start_pfn: u64,
    pages: c_size_t,
) -> *mut LLFree {
    const ALIGN: usize = align_of::<Align>();

    init_logging();

    if pages == 0 {
        return null_mut();
    }

    let offset = align_down(start_pfn as usize, 1 << LLFree::MAX_ORDER);
    let frames = pages as usize + (start_pfn as usize - offset);

    // Allocate metadata
    let MetaSize { primary, secondary } = LLFree::metadata_size(cores, frames);
    let primary_buf = unsafe { llfree_linux_alloc(node, primary, ALIGN) };
    let p = unsafe { slice::from_raw_parts_mut(primary_buf, primary) };
    let secondary_buf = unsafe { llfree_linux_alloc(node, primary, ALIGN) };
    let s = unsafe { slice::from_raw_parts_mut(secondary_buf, primary) };

    warn!("setup mem={offset:x} ({frames})");
    match LLFree::new(cores, frames, AllocAll, p, s) {
        Ok(alloc) => unsafe {
            // Move to newly allocated memory and leak address
            let dst = llfree_linux_alloc(node, size_of::<LLFree>(), ALIGN);
            core::ptr::write(dst.cast(), alloc);
            dst.cast()
        },
        Err(e) => unsafe {
            llfree_linux_free(primary_buf, primary, ALIGN);
            llfree_linux_free(secondary_buf, secondary, ALIGN);
            error!("init failed: {e:?}");
            null_mut()
        },
    }
}

/// Shut down the allocator normally.
#[cold]
#[no_mangle]
pub extern "C" fn llfree_uninit(alloc: *mut LLFree) {
    if !alloc.is_null() {
        unsafe {
            core::ptr::drop_in_place(alloc);
            llfree_linux_free(alloc.cast(), size_of::<LLFree>(), align_of::<Align>());
        }
    }
}

/// Allocates 2^order pages. Returns >=PAGE_SIZE on success an error code.
#[no_mangle]
pub extern "C" fn llfree_get(alloc: *const LLFree, core: c_size_t, order: c_size_t) -> result_t {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        alloc.get(core, order).into()
    } else {
        Error::Initialization.into()
    }
}

/// Frees a previously allocated page. Returns 0 on success or an error code.
#[no_mangle]
pub extern "C" fn llfree_put(
    alloc: *const LLFree,
    core: c_size_t,
    frame: u64,
    order: c_size_t,
) -> result_t {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        alloc.put(core, frame as _, order).into()
    } else {
        Error::Initialization.into()
    }
}

#[cold]
#[no_mangle]
pub extern "C" fn llfree_cores(alloc: *const LLFree) -> c_size_t {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        alloc.cores()
    } else {
        0
    }
}

#[cold]
#[no_mangle]
pub extern "C" fn llfree_frames(alloc: *const LLFree) -> c_size_t {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        alloc.cores()
    } else {
        0
    }
}

#[cold]
#[no_mangle]
pub extern "C" fn llfree_free_frames(alloc: *const LLFree) -> c_size_t {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        alloc.free_frames()
    } else {
        0
    }
}

#[cold]
#[no_mangle]
pub extern "C" fn llfree_free_huge(alloc: *const LLFree) -> c_size_t {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        alloc.free_huge_frames()
    } else {
        0
    }
}

#[no_mangle]
pub extern "C" fn llfree_is_free(alloc: *const LLFree, frame: u64, order: c_size_t) -> bool {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        alloc.is_free(frame as _, order)
    } else {
        false
    }
}

#[cold]
#[no_mangle]
pub extern "C" fn llfree_free_at(alloc: *const LLFree, frame: u64, order: c_size_t) -> c_size_t {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        alloc.free_at(frame as _, order)
    } else {
        0
    }
}

#[no_mangle]
pub extern "C" fn llfree_drain(alloc: *const LLFree, core: c_size_t) -> result_t {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        alloc.drain(core).into()
    } else {
        Error::Initialization.into()
    }
}

#[cold]
#[no_mangle]
pub extern "C" fn llfree_printk(alloc: *const LLFree) {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        warn!("{alloc:?}");
    }
}

/// # Safety
/// This writes into the provided memory buffer which has to be valid.
#[cold]
#[no_mangle]
pub extern "C" fn llfree_dump(alloc: *const LLFree, buf: *mut u8, len: c_size_t) -> c_size_t {
    if let Some(alloc) = unsafe { alloc.as_ref() } {
        let mut writer =
            unsafe { RawFormatter::from_ptrs(buf, (buf as usize).saturating_add(len) as _) };
        if writeln!(writer, "{alloc:?}").is_err() {
            error!("write failed after {}B", writer.bytes_written());
        }
        writer.bytes_written()
    } else {
        0
    }
}

#[inline(never)]
#[panic_handler]
pub fn panic(info: &PanicInfo) -> ! {
    error!("{info}");
    unsafe { llfree_panic() }
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
