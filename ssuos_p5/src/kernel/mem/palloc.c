#include <mem/palloc.h>
#include <bitmap.h>
#include <type.h>
#include <round.h>
#include <mem/mm.h>
#include <synch.h>
#include <device/console.h>
#include <mem/paging.h>
#include <proc/proc.h>

/* Page allocator.  Hands out memory in page-size (or
   page-multiple) chunks.  
   */

/* page struct */
struct kpage{
	uint32_t type;
	uint32_t *vaddr;
	uint32_t nalloc;
	pid_t pid;
};


static struct kpage *kpage_list;
static uint32_t page_alloc_index;

/* Initializes the page allocator. */
	void
init_palloc (void) 
{
	/* Calculate the space needed for the kpage list */
	size_t pool_size = sizeof(struct kpage) * PAGE_POOL_SIZE;

	/* kpage list alloc */
	kpage_list = (struct kpage *)(KERNEL_ADDR);

	/* initialize */
	memset((void*)kpage_list, 0, pool_size);
	page_alloc_index = 0;
}

/* Obtains and returns a group of PAGE_CNT contiguous free pages.
   */
	uint32_t *
palloc_get_multiple (uint32_t page_type, size_t page_cnt)
{
	void *pages = NULL;
	struct kpage *kpage = kpage_list;
	size_t page_idx = 0;
	int i = 0,j = 0;
	
	if (page_cnt == 0)
		return NULL;
	
	//비어있는 kpage 찾기
	while (j < page_alloc_index) {
		kpage = (struct kpage *)((uint32_t)kpage_list + sizeof(struct kpage) * i);			
				
		if ((kpage->type&FREE__) == FREE__)
			if (kpage->nalloc == page_cnt && (kpage->type&page_type) == page_type)
				break;

		if ((kpage->type&page_type) == page_type)
			page_idx += kpage->nalloc;

		j += kpage->nalloc;
		i++;
	}

	if (page_alloc_index == 0) {
		kpage = (struct kpage *)((uint32_t)kpage_list);
		page_alloc_index += page_cnt;
	}
	else if (j >= page_alloc_index) {
		kpage = (struct kpage *)((uint32_t)kpage_list + sizeof(struct kpage) * i);
		page_alloc_index += page_cnt;
	}
		
	
	page_idx += page_cnt;

	switch(page_type){
		case HEAP__: //(1)			
			pages = (void*)(VKERNEL_HEAP_START - (page_idx) * PAGE_SIZE);
			kpage->vaddr = pages;
			kpage->nalloc = page_cnt;
			kpage->type = HEAP__;
			kpage->pid = cur_process->pid;
			//printk("Palloc kpage[%d] heap -> ", i);
			//printk("%x , ra -> ", pages);
			//printk("%x\n", va_to_ra(pages));
			memset((void *)pages, 0, page_cnt * PAGE_SIZE);
			break;
		case STACK__:
			pages = (void*)(VKERNEL_STACK_ADDR);
			kpage->vaddr = pages;
			kpage->nalloc = page_cnt;
			kpage->type = STACK__;
			kpage->pid = cur_process->pid;
			//printk("Palloc kpage[%d] stack -> ", i);
			//printk("%x , ra -> ", pages);
			//printk("%x\n", va_to_ra(pages));
			memset((void *)pages - page_cnt * PAGE_SIZE, 0, page_cnt * PAGE_SIZE);		//(2)
			break;
		default:
			return NULL;
	}

	return (uint32_t*)pages; 
}

/* Obtains a single free page and returns its address.
   */
	uint32_t *
palloc_get_page (uint32_t page_type) 
{
	return palloc_get_multiple (page_type, 1);
}

/* Frees the PAGE_CNT pages starting at PAGES. */
	void
palloc_free_multiple (void *pages, size_t page_cnt) 
{

	struct kpage *kpage = kpage_list;
	int i = 0, j = 0;	
	
	if (pages == NULL || page_cnt == 0)
		return;
	//page 같은 것 찾기
	while (j < page_alloc_index) {	
		kpage = (struct kpage *)((uint32_t)kpage_list + sizeof(struct kpage) * i);

		if (kpage->vaddr == pages && kpage->nalloc == page_cnt) {
			kpage->type = kpage->type|FREE__;//FREE__ 추가		
			break;
		}		
		
		j += kpage->nalloc;
		i++;
	}

}

/* Frees the page at PAGE. */
	void
palloc_free_page (void *page) 
{
	palloc_free_multiple (page, 1);
}


	uint32_t *
va_to_ra (uint32_t *va){
	struct kpage *kpage = kpage_list;
	int i = 0, j = 0; 
	//printk("call va_to_ra, %x\n", va);
	if ((uint32_t)va >= 0 && (uint32_t)va < RKERNEL_HEAP_START)
		return va;
	//kpage 순회
	while (j < page_alloc_index) {
		kpage = (struct kpage *)((uint32_t)kpage_list + sizeof(struct kpage) * i);
		
		if (kpage->vaddr == va) {
			//힙 리 턴
			if ((kpage->type&HEAP__) == HEAP__)
				return (uint32_t *)(j * PAGE_SIZE + RKERNEL_HEAP_START);
		}
		//스 택 리 턴
		if (va <= VKERNEL_STACK_ADDR && va >= (VKERNEL_STACK_ADDR - 2 * PAGE_SIZE)) {
			if ((kpage->type&STACK__) == STACK__) {
				if (kpage->pid == cur_process->pid)
					return (uint32_t *)(j * PAGE_SIZE + RKERNEL_HEAP_START + ((uint32_t*)VKERNEL_STACK_ADDR - va));
			}
		}
		j += kpage->nalloc;
		i++;
	}
	
	return NULL;

}
		
	uint32_t *
ra_to_va (uint32_t *ra){
	struct kpage *kpage = kpage_list;
	int i = 0, j = 0, idx;	
	int s = 0, h = 0;
	//printk("call ra_to_va, %x\n", ra);
	//1:1 매칭
	if ((uint32_t)ra >= 0 && (uint32_t)ra < RKERNEL_HEAP_START)
		return ra;
	//인덱스 찾기
	idx = (int)(((uint32_t)ra - RKERNEL_HEAP_START) / PAGE_SIZE);
	//인덱스의 kpage찾기
	while (j < page_alloc_index) {
		kpage = (struct kpage *)((uint32_t)kpage_list + sizeof(struct kpage) * i);
		
		if (j == idx)
			break;

		if ((kpage->type&HEAP__) == HEAP__)
			h += kpage->nalloc;
		
		j += kpage->nalloc;
		i++;
	}

	if (j == 0)
		j += kpage->nalloc;

	if ((kpage->type&STACK__) == STACK__)
		return (uint32_t *)(VKERNEL_STACK_ADDR);
	if ((kpage->type&HEAP__) == HEAP__)
		return (uint32_t *)(VKERNEL_HEAP_START - (h + kpage->nalloc) * PAGE_SIZE);

	return NULL;

}

void palloc_pf_test(void)
{
	uint32_t *one_page1 = palloc_get_page(HEAP__);
	uint32_t *one_page2 = palloc_get_page(HEAP__);
	uint32_t *two_page1 = palloc_get_multiple(HEAP__,2);
	uint32_t *three_page;
	
	printk("one_page1 = %x\n", one_page1); 
	printk("one_page2 = %x\n", one_page2); 
	printk("two_page1 = %x\n", two_page1);

	printk("=----------------------------------=\n");
	palloc_free_page(one_page1);
	palloc_free_page(one_page2);
	palloc_free_multiple(two_page1,2);

	one_page1 = palloc_get_page(HEAP__);
	two_page1 = palloc_get_multiple(HEAP__,2);
	one_page2 = palloc_get_page(HEAP__);

	printk("one_page1 = %x\n", one_page1);
	printk("one_page2 = %x\n", one_page2);
	printk("two_page1 = %x\n", two_page1);

	printk("=----------------------------------=\n");
	three_page = palloc_get_multiple(HEAP__,3);

	printk("three_page = %x\n", three_page);
	palloc_free_page(one_page1);
	palloc_free_page(one_page2);
	palloc_free_multiple(two_page1,2);
	palloc_free_multiple(three_page, 3);
	
}
