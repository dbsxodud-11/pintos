/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

static bool
lazy_load_file (struct page *page, void *aux) {
	struct necessary_info *info = (struct necessary_info *)aux;
	
	struct file *file = info->file;
	// printf("After: %p\n", file);
	off_t ofs = info->ofs;
	size_t page_read_bytes = info->page_read_bytes;
	size_t page_zero_bytes = info->page_zero_bytes;

	file_seek (file, ofs);
	if (file_read (file, page->frame->kva, page_read_bytes) != page_read_bytes) {
		ASSERT(0);
		return false;
	}
	memset(page->frame->kva + page_read_bytes, 0, page_zero_bytes);
	return true;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {
	void *addr_copy = addr;
	size_t read_bytes = length > file_length (file) ? file_length (file) : length;
    size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;
	int count;

	if (read_bytes == 0)
		return NULL;

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_file. */
		struct necessary_info *info = malloc(sizeof (struct necessary_info));
		info->file = file_reopen (file);
		info->ofs = offset;
		info->page_read_bytes = page_read_bytes;
		info->page_zero_bytes = page_zero_bytes;
		// printf("Before: %p\n", info->file);

		if (!vm_alloc_page_with_initializer (VM_FILE, addr,
					writable, lazy_load_file, info)) {
			return NULL;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += PGSIZE;
		count += 1;
	}
	
	return addr_copy;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct thread *curr = thread_current ();
	struct page *page = spt_find_page (&curr->spt, addr);
	if (page == NULL)
		return;

	if (page->uninit.type != VM_FILE || page->uninit.aux == NULL)
		return;

	struct necessary_info *info = page->uninit.aux;
        
	if (pml4_is_dirty (curr->pml4, page->va)) {
		// mmap'd regions are only written back on munmap if the data was actually modified in memory
		file_write_at(info->file, addr, info->page_read_bytes, info->ofs);
		pml4_set_dirty (curr->pml4, page->va, true);
	}
	pml4_clear_page (curr->pml4, addr);
}
