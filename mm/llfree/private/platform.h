#pragma once

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/printk.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <asm/page_types.h>
#include <asm/pgtable_types.h>

#define UINT64_MAX 0xffffffffffffffffllu
#define PRIu64 "llu"
#define PRIx64 "llx"
#define PRId64 "lld"
#define PRIuS "zu"
#define PRIxS "zx"

/// Number of Bytes in cacheline
#define LLFREE_CACHE_SIZE 64u

#define LLFREE_FRAME_BITS PAGE_SHIFT
/// Size of a base frame
#define LLFREE_FRAME_SIZE (1u << LLFREE_FRAME_BITS)

/// Order of a huge frame
#define LLFREE_HUGE_ORDER HUGETLB_PAGE_ORDER
/// Maximum order that can be allocated
#define LLFREE_MAX_ORDER (LLFREE_HUGE_ORDER + 1u)

/// Num of bits of the larges atomic type of the architecture
#define LLFREE_ATOMIC_ORDER 6u
#define LLFREE_ATOMIC_SIZE (1u << LLFREE_ATOMIC_ORDER)

/// Number of frames in a child
#define LLFREE_CHILD_ORDER LLFREE_HUGE_ORDER
#define LLFREE_CHILD_SIZE (1u << LLFREE_CHILD_ORDER)

/// Number of frames in a tree
#define LLFREE_TREE_CHILDREN_ORDER 4u
#define LLFREE_TREE_CHILDREN (1u << LLFREE_TREE_CHILDREN_ORDER)
#define LLFREE_TREE_ORDER (LLFREE_HUGE_ORDER + LLFREE_TREE_CHILDREN_ORDER)
#define LLFREE_TREE_SIZE (1u << LLFREE_TREE_ORDER)

/// Minimal alignment the llc requires for its memory range
#define LLFREE_ALIGN (1u << LLFREE_MAX_ORDER << LLFREE_FRAME_BITS)

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

#endif

/// Iterates over a Range between multiples of len starting at idx.
///
/// Starting at idx up to the next Multiple of len (exclusive). Then the next
/// step will be the highest multiple of len less than idx. (_base_idx)
/// Loop will end after len iterations.
/// code will be executed in each loop.
/// The current loop value can accessed by current_i
#define for_offsetted(idx, len)                                   \
	for (size_t _i = 0, _offset = (idx) % (len),              \
		    _base_idx = (idx)-_offset, current_i = (idx); \
	     _i < (len);                                          \
	     _i = _i + 1, current_i = _base_idx + ((_i + _offset) % (len)))

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

#define atom_and(obj, mask)                                           \
	({                                                            \
		llfree_debug("and");                                  \
		__c11_atomic_fetch_and(obj, mask, ATOM_UPDATE_ORDER); \
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
