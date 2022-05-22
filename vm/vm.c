/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "userprog/syscall.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init (&frame_list);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *page = malloc(sizeof (struct page));
		switch (VM_TYPE(type)) {
			case VM_ANON:
				uninit_new (page, upage, init, type, aux, anon_initializer);
				break;
			case VM_FILE:
				uninit_new (page, upage, init, type, aux, file_backed_initializer);
				break;
		}
		page->writable = writable;
		/* TODO: Insert the page into the spt. */
		spt_insert_page (spt, page);
		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	/* TODO: Fill this function. */
	struct page *fake_page = malloc(sizeof (struct page));
	fake_page->va = pg_round_down (va);

	struct hash_elem *e = hash_find (&spt->hash_for_spt, &fake_page->hash_elem);
	free(fake_page);

	if (e != NULL)
		return hash_entry (e, struct page, hash_elem);
	else
		return NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt, struct page *page) {
	int succ = false;
	/* TODO: Fill this function. */
	if (hash_insert (&spt->hash_for_spt, &page->hash_elem) == NULL)
		succ = true;
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	struct list_elem *e = list_pop_front (&frame_list); // FIFO Policy
	victim = list_entry (e, struct frame, frame_elem);

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out (victim->page);
	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	/* TODO: Fill this function. */
	void *kva = palloc_get_page (PAL_USER);
	if (kva == NULL) {
		struct frame *frame = vm_evict_frame ();
		frame->page = NULL;
		return frame;
	}

	struct frame *frame = malloc(sizeof (struct frame));
	frame->kva = kva;
	frame->page = NULL;

	list_push_back (&frame_list, &frame->frame_elem);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth () {
	void *new_stack_bottom = thread_current ()->stack_bottom - PGSIZE;

	if (vm_alloc_page (VM_ANON, new_stack_bottom, true)) {
		vm_claim_page (new_stack_bottom);
		thread_current ()->stack_bottom = new_stack_bottom;
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr, bool user, bool write, bool not_present) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if (not_present) {
		if (!vm_claim_page (addr)) {
			if (f->rsp - 8 <= addr && USER_STACK - 0x100000 <= addr && addr <= USER_STACK) {
                vm_stack_growth ();
                return true;
            }
			else {
				return false;
			}
		}
		else {
			return true;
		}
	}
	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va) {
	/* TODO: Fill this function */
	struct thread *curr = thread_current ();
	struct page *page = spt_find_page (&curr->spt, va);
	if (page == NULL)
		return false;

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread *curr = thread_current ();
	if (!pml4_set_page (curr->pml4, page->va, frame->kva, page->writable))
		return false;

	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	hash_init (&spt->hash_for_spt, hash_hash_func_for_spt, hash_less_func_for_spt, NULL);
}

uint64_t 
hash_hash_func_for_spt (const struct hash_elem *e, void *aux) {
	struct page *page = hash_entry (e, struct page, hash_elem);
	return hash_bytes (&page->va, sizeof (page->va));
}

bool
hash_less_func_for_spt (const struct hash_elem *a, const struct hash_elem *b, void *aux) {
	if (hash_entry (a, struct page, hash_elem)->va < hash_entry (b, struct page, hash_elem)->va)
		return true;
	else
		return false;
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst, struct supplemental_page_table *src) {
	struct hash_iterator iter;
	hash_first (&iter, &src->hash_for_spt);

	while (hash_next (&iter)) {
		struct page *parent_page = hash_entry (hash_cur (&iter), struct page, hash_elem);
		if (parent_page->operations->type == VM_UNINIT) {
			struct necessary_info *parent_info = parent_page->uninit.aux;
			struct necessary_info *child_info = malloc(sizeof (struct necessary_info));
			child_info->file = file_reopen (parent_info->file);
			child_info->ofs = parent_info->ofs;
			child_info->page_read_bytes = parent_info->page_read_bytes;
			child_info->page_zero_bytes = parent_info->page_zero_bytes;
			vm_alloc_page_with_initializer (parent_page->uninit.type, parent_page->va, parent_page->writable,
											parent_page->uninit.page_initializer, child_info);
			vm_claim_page (parent_page->va);
		}
		else {
			vm_alloc_page (parent_page->uninit.type, parent_page->va, parent_page->writable);
			vm_claim_page (parent_page->va);
			struct page *child_page = spt_find_page (dst, parent_page->va);
			ASSERT (parent_page	->frame != NULL);
			memcpy (child_page->frame->kva, parent_page->frame->kva, PGSIZE);
		}
		// struct page *child_page = spt_find_page (dst, parent_page->va);
		// ASSERT (parent_page->frame != NULL);
		// memcpy (child_page->frame->kva, parent_page->frame->kva, PGSIZE);
	}
	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	if (hash_empty (&spt->hash_for_spt))
		return;
	
	struct hash_iterator iter;
	hash_first (&iter, &spt->hash_for_spt);

	while (hash_next (&iter)) {
		struct page *page = hash_entry (hash_cur (&iter), struct page, hash_elem);
		destroy (page);
		// hash_delete (&spt->hash_for_spt, &page->hash_elem);
	}
	hash_init (&spt->hash_for_spt, hash_hash_func_for_spt, hash_less_func_for_spt, NULL);
}
