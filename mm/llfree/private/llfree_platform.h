#pragma once

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/printk.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <asm/page_types.h>
#include <asm/pgtable_types.h>

#define ll_align(align) __attribute__((aligned(align)))
#define unlikely(x) __builtin_expect(!!(x), 0)

#define UINT64_MAX 0xffffffffffffffffllu
#define PRIu64 "llu"
#define PRIx64 "llx"
#define PRId64 "lld"
#define PRIuS "zu"
#define PRIxS "zx"

#define llfree_warn(str, ...) pr_warn(str, ##__VA_ARGS__)

#define VERBOSE 1
#ifdef VERBOSE
#define llfree_info_start() pr_info("")
#define llfree_info_cont(str, ...) pr_cont(str, ##__VA_ARGS__)
#define llfree_info_end()
#define llfree_info(str, ...) pr_info(str, ##__VA_ARGS__)
#else
#define llfree_info(str, ...)
#define llfree_info_start()
#define llfree_info_cont(str, ...)
#define llfree_info_end()
#endif

#ifdef DEBUG
#define llfree_debug(str, ...) pr_debug(str, ##__VA_ARGS__)
#else
#define llfree_debug(str, ...)
#endif

void noinline llfree_panic(void);

#define assert(cond)                     \
	do {                             \
		if (unlikely(!(cond))) { \
			llfree_panic();  \
			BUG();           \
		}                        \
	} while (0)

static const int ATOM_LOAD_ORDER = __ATOMIC_ACQUIRE;
static const int ATOM_UPDATE_ORDER = __ATOMIC_ACQ_REL;
static const int ATOM_STORE_ORDER = __ATOMIC_RELEASE;

/* GCC compatibility */
#if !defined(__clang__) && defined(__GNUC__)

#define __c11_atomic_compare_exchange_strong(obj, expected, desired,         \
					     order_success, order_failure)   \
	__extension__({                                                      \
		__auto_type __atomic_compare_exchange_ptr = (obj);           \
		__typeof__((void)0, *__atomic_compare_exchange_ptr)          \
			__atomic_compare_exchange_tmp = (desired);           \
		__atomic_compare_exchange(__atomic_compare_exchange_ptr,     \
					  (expected),                        \
					  &__atomic_compare_exchange_tmp, 0, \
					  (order_success), (order_failure)); \
	})

#define __c11_atomic_compare_exchange_weak(obj, expected, desired,           \
					   order_success, order_failure)     \
	__extension__({                                                      \
		__auto_type __atomic_compare_exchange_ptr = (obj);           \
		__typeof__((void)0, *__atomic_compare_exchange_ptr)          \
			__atomic_compare_exchange_tmp = (desired);           \
		__atomic_compare_exchange(__atomic_compare_exchange_ptr,     \
					  (expected),                        \
					  &__atomic_compare_exchange_tmp, 1, \
					  (order_success), (order_failure)); \
	})

#define __c11_atomic_load(obj, order)                                          \
	__extension__({                                                        \
		__auto_type __atomic_load_ptr = (obj);                         \
		__typeof__((void)0, *__atomic_load_ptr) __atomic_load_tmp;     \
		__atomic_load(__atomic_load_ptr, &__atomic_load_tmp, (order)); \
		__atomic_load_tmp;                                             \
	})

#define __c11_atomic_store(obj, val, order)                                   \
	__extension__({                                                       \
		__auto_type __atomic_store_ptr = (obj);                       \
		__typeof__((void)0, *__atomic_store_ptr) __atomic_store_tmp = \
			(val);                                                \
		__atomic_store(__atomic_store_ptr, &__atomic_store_tmp,       \
			       (order));                                      \
	})

#define __c11_atomic_exchange(obj, val, order)                                \
	__extension__({                                                       \
		__auto_type __atomic_exchange_ptr = (obj);                    \
		__typeof__((void)0, *__atomic_exchange_ptr) __atomic_exchange_tmp = \
			(val);                                                \
		__atomic_exchange(__atomic_exchange_ptr, &__atomic_exchange_tmp, \
				   &__atomic_exchange_tmp, (order));            \
		__atomic_exchange_tmp;                                        \
	}

#endif

/// Checks if `obj` contains `expected` and writes `disired` to it if so.
#define atom_cmp_exchange(obj, expected, desired)                       \
	({                                                              \
		llfree_debug("cmpxchg");                                \
		__c11_atomic_compare_exchange_strong((obj), (expected), \
						     (desired),         \
						     ATOM_UPDATE_ORDER, \
						     ATOM_LOAD_ORDER);  \
	})
/// Checks if `obj` contains `expected` and writes `disired` to it if so.
#define atom_cmp_exchange_weak(obj, expected, desired)                \
	({                                                            \
		llfree_debug("cmpxchg");                              \
		__c11_atomic_compare_exchange_weak((obj), (expected), \
						   (desired),         \
						   ATOM_UPDATE_ORDER, \
						   ATOM_LOAD_ORDER);  \
	})

#define atom_load(obj)                                   \
	({                                               \
		llfree_debug("load");                    \
		__c11_atomic_load(obj, ATOM_LOAD_ORDER); \
	})
#define atom_store(obj, val)                                    \
	({                                                      \
		llfree_debug("store");                          \
		__c11_atomic_store(obj, val, ATOM_STORE_ORDER); \
	})

#define atom_swap(obj, desired)                                        \
	({                                                             \
		llfree_debug("swap");                                   \
		__c11_atomic_exchange(obj, desired, ATOM_UPDATE_ORDER); \
	})

/// Atomic fetch-modify-update macro.
///
/// This macro loads the value at `atom_ptr`, stores its llfree_result in `old_val`
/// and then executes the `fn` function with a pointer to the loaded value,
/// which should be modified and is then stored atomically with CAS.
/// The function `fn` can take any number of extra parameters,
/// that are passed directly into it.
///
/// Returns if the update was successfull.
/// Fails only if `fn` returns false.
///
/// Example:
/// ```
/// bool my_update(uint64_t *value, bool argument1, int argument 2) {
/// 	if (argument1) {
///     	*value *= *value;
///		return true;
///	}
///     return false;
/// }
///
/// _Atomic uint64_t my_atomic;
/// uint64_t old;
/// if (!atom_update(&my_atomic, old, my_update, false, 42)) {
/// 	assert(!"our my_update function returned false, cancelling the update");
/// }
/// printf("old value %u\n", old);
/// ```
#define atom_update(atom_ptr, old_val, fn, ...)                            \
	({                                                                 \
		/* NOLINTBEGIN */                                          \
		llfree_debug("update");                                    \
		bool _ret = false;                                         \
		(old_val) = atom_load(atom_ptr);                           \
		while (true) {                                             \
			__typeof(old_val) value = (old_val);               \
			if (!(fn)(&value, ##__VA_ARGS__))                  \
				break;                                     \
			if (atom_cmp_exchange_weak((atom_ptr), &(old_val), \
						   value)) {               \
				_ret = true;                               \
				break;                                     \
			}                                                  \
		}                                                          \
		_ret;                                                      \
		/* NOLINTEND */                                            \
	})
