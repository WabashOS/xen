/* Copyright (C) 2004 Christopher Clark <firstname.lastname@cl.cam.ac.uk> */

/*
 * There are duplicates of this code in:
 *  - tools/xenstore/hashtable.c
 *  - xen/tools/blktap2/drivers/FILE_NAME
 */

/* 
 * 2009: Modified by Barret Rhoden <brho@cs.berkeley.edu>
 * Changes: 
 * - Added APPLY_MAX_LOAD_FACTOR macro to avoid use of ceil() function.
 * - No longer frees keys or values. Keys and values must be allocated and freed
 *   by the callee.
 * - Used a slab allocator (aka. a pool) for hashtable entry allocation.
 * - Added the generic hash and equality functions (meant for longs).
 *
 * 
 * 11/15/2013: Brought it from xen/tools/blktap2/drivers/ into the Xen
 * hypervisor code by Juan A. Colmenares <juancol@eecs.berkeley.edu>. 
 * Changes:
 * - Included Xen header files. 
 * - Used xmalloc, xzalloc_array and xfree functions instead of malloc, free,
 *   and memset.
 * - Used the  struct xmem_pool for the entry allocation and free.
 */

#include <xen/hashtable.h>
#include <xen/hashtable_private.h>

#include <xen/mm.h>


/*
 Credit for primes table: Aaron Krowne
 http://br.endernet.org/~akrowne/
 http://planetmath.org/encyclopedia/GoodHashTablePrimes.html
*/
static const unsigned int primes[] = {
	53, 97, 193, 389,
	769, 1543, 3079, 6151,
    12289, 24593, 49157, 98317,
	196613, 393241, 786433, 1572869,
	3145739, 6291469, 12582917, 25165843,
	50331653, 100663319, 201326611, 402653189,
	805306457, 1610612741,
};

const unsigned int prime_table_length = sizeof(primes)/sizeof(primes[0]);

#define MAX_TABLE_CAPACITY (primes[prime_table_length - 1])  

#define APPLY_MAX_LOAD_FACTOR(size) ((size * 13)/20)


///////////////////////////////////////////////////////////////////////////////
// Variables and functions for the entry pool.
///////////////////////////////////////////////////////////////////////////////

#define ENTRY_POOL_NAME "ENTRY_POOL"

/** Amount of memory (in bytes) added to the entry pool whenever required. */
#define ENTRY_POOL_GROW_SIZE (PAGE_SIZE)

/** Callback function used to expand the entry pool. */
static 
void* entry_pool_get_memory(unsigned long bytes) {
    ASSERT(bytes == ENTRY_POOL_GROW_SIZE);
    return alloc_xenheap_pages(get_order_from_bytes(bytes), 0);
}

/** Callback function used to shrink the entry pool. */
static 
void entry_pool_put_memory(void* ptr) {
    ASSERT(ptr != NULL); 
    free_xenheap_pages(ptr, get_order_from_bytes(ENTRY_POOL_GROW_SIZE));  
}

///////////////////////////////////////////////////////////////////////////////



struct hashtable*
create_hashtable(unsigned int minsize,
                 unsigned int (*hashf) (void*),
                 int (*eqf) (void*,void*)) {
	struct hashtable *h;
	unsigned int pindex, size = primes[0];
	/* Check requested hashtable isn't too large */
	if (minsize > (1u << 30)) return NULL;
	/* Enforce size as prime */
	for (pindex=0; pindex < prime_table_length; pindex++) {
		if (primes[pindex] > minsize) { size = primes[pindex]; break; }
	}

	h = xmalloc(struct hashtable);
	if (h == NULL) return NULL; /*oom*/

	h->table = xzalloc_array(struct entry*, size);
	if (h->table == NULL) { 
        xfree(h); 
        return NULL; 
    } 

    h->entry_pool = xmem_pool_create(ENTRY_POOL_NAME, 
                                     entry_pool_get_memory, 
                                     entry_pool_put_memory, 
                                     size * sizeof(struct entry), 
                                     MAX_TABLE_CAPACITY * sizeof(struct entry),
                                     ENTRY_POOL_GROW_SIZE);
                                     
	if (h->entry_pool == NULL) { 
	    xfree(h->table);
    	xfree(h);
        return NULL; 
    } 

	h->tablelength  = size;
	h->primeindex   = pindex;
	h->entrycount   = 0;
	h->hashfn       = hashf;
	h->eqfn         = eqf;
	h->loadlimit = APPLY_MAX_LOAD_FACTOR(size);
	return h;
}


unsigned int generic_hash(void* k) {
    // 0x9e370001UL used by Linux (32 bit).
    // Prime approximation to the golden ratio to the maximum integer, 
    // IAW Knuth.
    uintptr_t kk = (uintptr_t) k; // To avoid pointer-to-int-cast error.
    return (unsigned int) kk * 0x9e370001UL;   
}


int generic_equal(void* k1, void* k2) {
    return k1 == k2;
}


unsigned int
hash(struct hashtable *h, void *k) {
	/* Aim to protect against poor hash functions by adding logic here
	 * - logic taken from java 1.4 hashtable source */
	unsigned int i = h->hashfn(k);
	i += ~(i << 9);
	i ^=  ((i >> 14) | (i << 18)); /* >>> */
	i +=  (i << 4);
	i ^=  ((i >> 10) | (i << 22)); /* >>> */
	return i;
}


static int
hashtable_expand(struct hashtable *h) {
	/* Double the size of the table to accomodate more entries */
	struct entry **newtable;
	struct entry *e;
	// struct entry **pE;
	unsigned int newsize, i, index;
	/* Check we're not hitting max capacity */
	if (h->primeindex == (prime_table_length - 1)) return 0;
	newsize = primes[++(h->primeindex)];

	newtable = (struct entry **) xzalloc_array(struct entry*, newsize);
	if (NULL != newtable) {
		/* This algorithm is not 'stable'. ie. it reverses the list
		 * when it transfers entries between the tables */
		for (i = 0; i < h->tablelength; i++) {
			while (NULL != (e = h->table[i])) {
				h->table[i] = e->next;
				index = indexFor(newsize,e->h);
				e->next = newtable[index];
				newtable[index] = e;
			}
		}
		xfree(h->table);
		h->table = newtable;
	}
	else {
        return 0;

        // TODO: Implement realloc in the Xen hypervisor code to handle this branch
        // properly.
    
        // Plan B: realloc instead.
        /*
        newtable = (struct entry **)
        	realloc(h->table, newsize * sizeof(struct entry *));
        if (NULL == newtable) { (h->primeindex)--; return 0; }
        h->table = newtable;
        memset(newtable[h->tablelength], 0, newsize - h->tablelength);
        for (i = 0; i < h->tablelength; i++) {
        	for (pE = &(newtable[i]), e = *pE; e != NULL; e = *pE) {
        		index = indexFor(newsize,e->h);
        		if (index == i)
        		{
        			pE = &(e->next);
        		}
        		else
        		{
        			*pE = e->next;
        			e->next = newtable[index];
        			newtable[index] = e;
        		}
        	}
        }
        */
	}
	h->tablelength = newsize;
    h->loadlimit = APPLY_MAX_LOAD_FACTOR(newsize);
	return -1;
}


unsigned int
hashtable_count(struct hashtable *h) {
	return h->entrycount;
}


int
hashtable_insert(struct hashtable *h, void *k, void *v) {
	/* This method allows duplicate keys - but they shouldn't be used */
	unsigned int index;
	struct entry *e;
	if (++(h->entrycount) > h->loadlimit)
	{
		/* Ignore the return value. If expand fails, we should
		 * still try cramming just this value into the existing table
		 * -- we may not have memory for a larger table, but one more
		 * element may be ok. Next time we insert, we'll try expanding again.*/
		hashtable_expand(h);
	}
    
    e = (struct entry*) xmem_pool_alloc(sizeof(struct entry), h->entry_pool);
	if (NULL == e) { 
        /*oom*/
        --(h->entrycount); 
        return 0; 
    } 
	e->h = hash(h,k);
	index = indexFor(h->tablelength,e->h);
	e->k = k;
	e->v = v;
	e->next = h->table[index];
	h->table[index] = e;
	return -1;
}


void* 
hashtable_search(struct hashtable *h, void *k) {
    // This function returns value associated with key.

	struct entry *e;
	unsigned int hashvalue, index;
	hashvalue = hash(h,k);
	index = indexFor(h->tablelength,hashvalue);
	e = h->table[index];
	while (NULL != e) {
		// Check hash value to short circuit heavier comparison.
		if ((hashvalue == e->h) && (h->eqfn(k, e->k))) return e->v;
		e = e->next;
	}
	return NULL;
}


void* 
hashtable_remove(struct hashtable *h, void *k) {
    // This function returns value associated with key.

	// TODO: Consider compacting the table when the load factor drops enough,
	//       or provide a 'compact' method. 
	struct entry *e;
	struct entry **pE;
	void *v;
	unsigned int hashvalue, index;

	hashvalue = hash(h,k);
	index = indexFor(h->tablelength,hash(h,k));
	pE = &(h->table[index]);
	e = *pE;
	while (NULL != e) {
		// Check hash value to short circuit heavier comparison.
		if ((hashvalue == e->h) && (h->eqfn(k, e->k))) {
			*pE = e->next;
			h->entrycount--;
			v = e->v;
            xmem_pool_free(e, h->entry_pool);
			return v;
		}
		pE = &(e->next);
		e = e->next;
	}
	return NULL;
}


void
hashtable_destroy(struct hashtable *h) {
    unsigned int i;
    struct entry *e, *f;
    struct entry **table = h->table;
    for (i = 0; i < h->tablelength; i++) {
        e = table[i];
        while (NULL != e) { 
          f = e; 
          e = e->next; 
          xmem_pool_free(f, h->entry_pool);
        }
    }
    xfree(h->table);
    xfree(h);
}


/*
 * Copyright (c) 2002, Christopher Clark
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of the original author; nor the names of any contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
