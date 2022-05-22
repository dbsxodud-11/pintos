/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/vaddr.h"
#include "lib/kernel/bitmap.h"

#define PAGE_SECTOR_SIZE (PGSIZE / DISK_SECTOR_SIZE)

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get (1, 1);
	swap_table = bitmap_create (PAGE_SECTOR_SIZE);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->sector = -1;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	int swap_sector = anon_page->sector;
	if (swap_sector == -1)
		return false;

	for (int i=0; i<PAGE_SECTOR_SIZE; i++) {
		disk_read (swap_disk, swap_sector + i, page->frame->kva + PAGE_SECTOR_SIZE * i);
	}

	bitmap_set_all (swap_table, false);
	pml4_set_page (thread_current ()->pml4, page->va, page->frame->kva, page->writable);
	return true; 
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	int swap_sector = bitmap_scan (swap_table, 0, 1, true);
	if (swap_sector == BITMAP_ERROR)
		return false;
	
	for (int i=0; i<PAGE_SECTOR_SIZE; i++) {
		disk_write (swap_disk, swap_sector + i, page->frame->kva + PAGE_SECTOR_SIZE * i);
	}

	pml4_set_accessed (thread_current ()->pml4, page->va, false);
	pml4_clear_page(thread_current ()->pml4, page->va);
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
