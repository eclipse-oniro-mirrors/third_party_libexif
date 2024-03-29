/*
 * Copyright (C) 2024 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>
#include "exif-mnote-data-huawei.h"
#include "mnote-huawei-entry.h"
#include "mnote-huawei-data-type.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libexif/exif-byte-order.h>
#include <libexif/exif-utils.h>
#include <libexif/exif-data.h>

const int DATA_OR_OFFSET = 4;
const int HUAWEI_HEADER_OFFSET = 8;
const char HUAWEI_HEADER[] = { 'H', 'U', 'A', 'W', 'E', 'I', '\0', '\0',
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void memory_dump(const void *ptr, int len) 
{
	printf("%08X: ", 0);
    for (int i = 0; i < len; i++) {
        if (i % 8 == 0 && i != 0)
            printf(" ");
        if (i % 16 == 0 && i != 0) 
			printf("\n%08X: ", i);		        
        printf("%02x ", *((uint8_t *)ptr + i));
    }
    printf("\n");
}

void print_huawei_md(const ExifMnoteDataHuawei* n) 
{
	printf("ifd_tag: %04X, count:%d, ifd_pos:%p, ifd_size:%d\n", n->ifd_tag, n->count, n, n->ifd_size);
	MnoteHuaweiEntry *entries = n->entries;
	for (int i=0; i<n->count; i++) {
		printf("idx:%d, tag: %04X, type: %d, components:%ld\n", i, entries[i].tag, entries[i].format, entries[i].components);
		memory_dump(entries[i].data, entries[i].size);
		if (entries[i].md) {
			print_huawei_md((ExifMnoteDataHuawei*) entries[i].md);
		}
	}
}

static void
exif_mnote_data_huawei_clear (ExifMnoteDataHuawei *n)
{
	ExifMnoteData *d = (ExifMnoteData *) n;

	if (!n) return;

	if (n->entries) {
		for (unsigned int i = 0; i < n->count; i++) {
			if (n->entries[i].data) {
				exif_mem_free(d->mem, n->entries[i].data);
				n->entries[i].data = NULL;
				n->entries[i].size = 0;
			}

			if (n->entries[i].md) {
				exif_mnote_data_huawei_clear((ExifMnoteDataHuawei *) n->entries[i].md);
				exif_mem_free(d->mem, n->entries[i].md);
				n->entries[i].md = NULL;
			}
		}

		exif_mem_free (d->mem, n->entries);
		n->entries = NULL;
		n->count = 0;
		n->is_loaded = 0;
	}
}

static void
exif_mnote_data_huawei_free (ExifMnoteData *ne) 
{
	if (!ne) return;
	exif_mnote_data_huawei_clear ((ExifMnoteDataHuawei*) ne);
}

static void
exif_mnote_data_huawei_set_byte_order (ExifMnoteData *n, ExifByteOrder o)
{
	if (n) ((ExifMnoteDataHuawei *) n)->order = o;
}

static void
exif_mnote_data_huawei_set_offset (ExifMnoteData *n, unsigned int o)
{
	if (n) ((ExifMnoteDataHuawei *) n)->offset = o;
}

static void
exif_mnote_data_huawei_malloc_size_data (ExifMnoteData *ne, unsigned int *malloc_size) 
{
	
	ExifMnoteDataHuawei *n = (ExifMnoteDataHuawei *) ne;
	if (!n) {
		exif_log (ne->log, EXIF_LOG_CODE_CORRUPT_DATA, "ExifMnoteHuawei", "n is NULL");
		*malloc_size = 0;
		return;
	}

	unsigned int ifd_size = 2 + n->count * 12 + 4;
	*malloc_size = *malloc_size + ifd_size;

	for (int i = 0; i < n->count; i++) {
		if (n->entries[i].md) {
			exif_mnote_data_huawei_malloc_size_data(n->entries[i].md, malloc_size);
			ExifMnoteDataHuawei *t_n = n->entries[i].md;
			ifd_size += t_n->ifd_size;
		}
		size_t components_size = exif_format_get_size (n->entries[i].format) *
						n->entries[i].components;
		if (*malloc_size > 65536) {
			exif_log (ne->log, EXIF_LOG_CODE_CORRUPT_DATA, "ExifMnoteHuawei", "malloc_size: (%d) too big", *malloc_size);
			*malloc_size = 0;
			return;
		}

		if (components_size > DATA_OR_OFFSET) {
			ifd_size += components_size;
			*malloc_size = *malloc_size + components_size;
		}
	}
	n->ifd_size = ifd_size;
}

static void
exif_mnote_data_huawei_save_data (ExifMnoteData *ne, unsigned char *buf, unsigned int buf_size, const unsigned char *pOrder) 
{

	ExifMnoteDataHuawei *n = (ExifMnoteDataHuawei *) ne;
	if (!n || !buf || !buf_size || !pOrder) return;

	size_t  offset=0;
	const unsigned int ifd_data_offset = 2 + n->count * 12 + 4;
	unsigned int ifd_data_offset_increment = 0;

	/* Save the number of entries */
	exif_set_short (buf, n->order, (ExifShort) n->count);
	
	/* Save each entry */
	for (int i = 0; i < n->count; i++) {
		offset = 2 + i * 12;
		exif_set_short (buf + offset + 0, n->order, (ExifShort) n->entries[i].tag);
		exif_set_short (buf + offset + 2, n->order, (ExifShort) n->entries[i].format);
		exif_set_long  (buf + offset + 4, n->order, n->entries[i].components);
		offset += 8;

		size_t components_size = exif_format_get_size (n->entries[i].format) *
						n->entries[i].components;

		ExifLong t_offset = buf + ifd_data_offset + ifd_data_offset_increment - pOrder;
		if (n->entries[i].md) {
			exif_set_long (buf + offset, n->order, t_offset);
			ExifMnoteDataHuawei *t_n = n->entries[i].md;
			exif_mnote_data_huawei_save_data(n->entries[i].md, buf+ifd_data_offset + ifd_data_offset_increment, t_n->ifd_size, pOrder);
			ifd_data_offset_increment += t_n->ifd_size;
		}

		if (components_size > DATA_OR_OFFSET) {
			// write data offset
			exif_set_long (buf + offset, n->order, t_offset);

			// write data			
			if (n->entries[i].data)
				memcpy (buf + ifd_data_offset + ifd_data_offset_increment, n->entries[i].data, components_size);
			ifd_data_offset_increment += components_size;
			
		} else {
			memcpy (buf + offset, n->entries[i].data, components_size);
		}			
	}
}

static void
exif_mnote_data_huawei_save (ExifMnoteData *ne, unsigned char **buf, unsigned int *buf_size)
{

	ExifMnoteDataHuawei *n = (ExifMnoteDataHuawei*) ne;
	if (!n) return;

	unsigned char *t_buf = NULL;
	unsigned int malloc_size = sizeof(HUAWEI_HEADER);
	
	exif_mnote_data_huawei_malloc_size_data(ne, &malloc_size);
	if (!malloc_size) {
		exif_log (ne->log, EXIF_LOG_CODE_CORRUPT_DATA, "ExifMnoteHuawei", "The memory(%d) request failed", malloc_size);
		return;
	}

	t_buf = exif_mem_alloc (ne->mem, malloc_size);
	if (!t_buf) {
		EXIF_LOG_NO_MEMORY(ne->log, "ExifMnoteHuawei", malloc_size);
		return;
	}

	memcpy(t_buf, HUAWEI_HEADER, 8);
	if (n->order == EXIF_BYTE_ORDER_INTEL) {
		(t_buf + 8)[0] = 'I';
		(t_buf + 8)[1] = 'I';
	} else {
		(t_buf + 8)[0] = 'M';
		(t_buf + 8)[1] = 'M';
	}
	exif_set_short (t_buf + 10, n->order, 0x002a);
	exif_set_long (t_buf + 12, n->order, 8);

	exif_mnote_data_huawei_save_data(ne, t_buf+sizeof(HUAWEI_HEADER), n->ifd_size, t_buf+HUAWEI_HEADER_OFFSET);

	*buf = t_buf;
	*buf_size = malloc_size;
}

static int
exif_mnote_data_huawei_load_data (ExifMnoteData *ne, const unsigned char *buf, unsigned int buf_size, 
								  unsigned int* cur_ifd_data_offset, const unsigned int order_offset)
{
	size_t tcount, offset, ifd_size=0;
	int ret = 0;

	ExifMnoteDataHuawei *n = (ExifMnoteDataHuawei *) ne;
	const unsigned char *ifd_data = buf + *cur_ifd_data_offset;

	ExifShort count = exif_get_short (ifd_data, n->order);	
	if (count > 100) {
		exif_log (ne->log, EXIF_LOG_CODE_CORRUPT_DATA, "ExifMnoteHuawei", "Too much tags (%d) in Huawei MakerNote", count);
		return -1;
	}
	offset = 2;

	MnoteHuaweiEntry* entries = exif_mem_alloc (ne->mem, sizeof (MnoteHuaweiEntry) * count);
	if (!entries) {
		EXIF_LOG_NO_MEMORY(ne->log, "ExifMnoteHuawei", sizeof (MnoteHuaweiEntry) * count);
		return -1;
	}
	memset(entries, 0, sizeof (MnoteHuaweiEntry) * count);

	/* Parse the entries */
	tcount = 0;
	for (int i = 0; i < count; i++, offset += 12) {
		
		entries[tcount].tag        = exif_get_short (ifd_data + offset, n->order);
		entries[tcount].format     = exif_get_short (ifd_data + offset + 2, n->order);
		entries[tcount].components = exif_get_long (ifd_data + offset + 4, n->order);
		entries[tcount].order      = n->order;
		entries[tcount].parent_md  = n;

		size_t components_size = exif_format_get_size (entries[tcount].format) * entries[tcount].components;
		entries[tcount].size = components_size;
		if (!components_size) {
			exif_log (ne->log, EXIF_LOG_CODE_CORRUPT_DATA,
				  "ExifMnoteHuawei",
				  "Invalid zero-length tag size");
			continue;
		} 

		size_t t_offset = offset + HUAWEI_HEADER_OFFSET;
		if (components_size > 4) t_offset = exif_get_long (ifd_data + t_offset, n->order) + HUAWEI_HEADER_OFFSET;

		entries[tcount].data = exif_mem_alloc (ne->mem, components_size);
		if (!entries[tcount].data) {
			EXIF_LOG_NO_MEMORY(ne->log, "ExifMnoteHuawei", components_size);
			continue;
		}

		unsigned int t_size = 0;
		if (components_size > 4) {
			t_size = order_offset + t_offset - HUAWEI_HEADER_OFFSET + components_size;
			memcpy (entries[tcount].data, buf + order_offset + t_offset - HUAWEI_HEADER_OFFSET, components_size);	
		} else {
			t_size = (ifd_data - buf) + t_offset + components_size;
			memcpy (entries[tcount].data, ifd_data + t_offset, components_size);		
		}

		//check offset overflow
		if (t_size > buf_size) {
			exif_log (ne->log, EXIF_LOG_CODE_CORRUPT_DATA,
						"ExifMnoteHuawei", "OVERFLOW %d > %d", t_size, buf_size);
			return -1;
		}

		MnoteHuaweiTag tag = entries[tcount].tag;
		if (tag == MNOTE_HUAWEI_SCENE_INFO || tag == MNOTE_HUAWEI_FACE_INFO) {

			*cur_ifd_data_offset = order_offset + exif_get_long (entries[tcount].data, n->order);
			ExifMnoteDataHuawei* md = (ExifMnoteDataHuawei*)exif_mnote_data_huawei_new(ne->mem);

			if(!md) {
				exif_log (ne->log, EXIF_LOG_CODE_CORRUPT_DATA,
				  "ExifMnoteHuawei",
				  "Invalid zero-length tag size");
				  return -1;
			}

			md->order = n->order;
			ret = exif_mnote_data_huawei_load_data((ExifMnoteData*)md, buf, buf_size, cur_ifd_data_offset, order_offset);
			if (ret == -1) {
				exif_log (ne->log, EXIF_LOG_CODE_CORRUPT_DATA,
				  "ExifMnoteHuawei",
				  "exif_mnote_data_huawei_load_ failed");
				  return ret;
			}
			md->ifd_tag = tag;
			entries[tcount].md = md;
		}
		
		ifd_size += offset;
		++tcount;
	}
	/* Store the count of successfully parsed tags */
	n->count = tcount;
	n->entries = entries;
	n->is_loaded = 1;

	return 0;
}

static void
exif_mnote_data_huawei_load (ExifMnoteData *ne, const unsigned char *buf, unsigned int buf_size)
{
	unsigned int head_offset = 6;
	ExifMnoteDataHuawei *n = (ExifMnoteDataHuawei *) ne;
	if (!n || !buf || !buf_size) {
		exif_log (ne->log, EXIF_LOG_CODE_CORRUPT_DATA,
			  "ExifMnoteHuawei", "Short MakerNote");
		return;
	}
	
	n->ifd_tag = 0xffff;

	if ((buf[0] == 'I' && buf[1] == 'I') || (buf[0] == 'M' && buf[1] == 'M')) {
		head_offset = 0;
	}

	unsigned int order_offset = n->offset + head_offset + HUAWEI_HEADER_OFFSET;
	const void *pOrder = buf + order_offset;

	//read order
	if (!memcmp (pOrder, "II", 2))
		n->order = EXIF_BYTE_ORDER_INTEL;
	else if (!memcmp (pOrder, "MM", 2))
		n->order = EXIF_BYTE_ORDER_MOTOROLA;
	else {
		exif_log (ne->log, EXIF_LOG_CODE_CORRUPT_DATA,
			  "ExifMnoteHuawei", "Unknown order.");
		return;
	}

	/* Remove any old entries */
	exif_mnote_data_huawei_clear (n);

	unsigned int ifd_data_offset= n->offset + head_offset + sizeof(HUAWEI_HEADER);	
	int ret = exif_mnote_data_huawei_load_data(ne, buf, buf_size, &ifd_data_offset, order_offset);
	if (ret) {
		exif_log (ne->log, EXIF_LOG_CODE_CORRUPT_DATA,
			  "ExifMnoteHuawei", "entries exif_mnote_data_huawei_load_ failed");
		return;
	}
}

unsigned int
exif_mnote_data_huawei_count_data (ExifMnoteData *ne, MnoteHuaweiEntryCount* ec) 
{
	ExifMnoteDataHuawei *n = (ExifMnoteDataHuawei *) ne;
	if (!ne) return 0;
	 
	unsigned int count = n->count;
	for (int i = 0; i < n->count; i++) {
		if (ec && (ec->size > ec->idx)) {
			ec->entries[ec->idx] = &n->entries[i];
			ec->idx += 1;
		}

		ExifMnoteData *md = n->entries[i].md;
		if (md) {
			count += exif_mnote_data_huawei_count_data(md, ec);
		}
	}
	return count;
}

unsigned int
exif_mnote_data_huawei_count (ExifMnoteData *ne) 
{
	if (!ne) return 0;
	unsigned int count = exif_mnote_data_huawei_count_data(ne, NULL);
	return count;
};	

MnoteHuaweiEntry* exif_mnote_data_huawei_get_entry_by_tag_data (ExifMnoteDataHuawei *n, int *idx, const MnoteHuaweiTag tag) 
{ 
	MnoteHuaweiEntry* entry = NULL;
	if (!n) return NULL;	

	for (int i = 0; i < n->count; i++) {
		if (n->entries[i].tag == tag) {
			entry = &n->entries[i];
			break;
		}
		*idx += 1;

		ExifMnoteData *md = n->entries[i].md;
		if (md) {
			entry = exif_mnote_data_huawei_get_entry_by_tag_data((ExifMnoteDataHuawei *)md, idx, tag);
			if (entry) break;
		}
	}
	return entry;
}

MnoteHuaweiEntry* exif_mnote_data_huawei_get_entry_by_tag (ExifMnoteDataHuawei *n, const MnoteHuaweiTag tag) 
{
	int i = 0;
	return exif_mnote_data_huawei_get_entry_by_tag_data(n, &i, tag);
}

MnoteHuaweiEntry* exif_mnote_data_huawei_get_entry_by_index_data (ExifMnoteDataHuawei *n, int *idx, const int dest_idx) 
{ 
	MnoteHuaweiEntry* entry = NULL;
	if (!n) return NULL;	

	for (int i = 0; i < n->count; i++) {
		if (*idx == dest_idx) {
			entry = &n->entries[i];
			break;
		}
		*idx += 1;

		ExifMnoteData *md = n->entries[i].md;
		if (md) {
			entry = exif_mnote_data_huawei_get_entry_by_index_data((ExifMnoteDataHuawei *)md, idx, dest_idx);
			if (entry) break;
		}		
	}
	return entry;
}

MnoteHuaweiEntry* exif_mnote_data_huawei_get_entry_by_index (ExifMnoteDataHuawei *n, const int dest_idx) 
{
	int i = 0;
	return exif_mnote_data_huawei_get_entry_by_index_data(n, &i, dest_idx);
}

void mnote_huawei_get_entry_count (const ExifMnoteDataHuawei* n, MnoteHuaweiEntryCount** entry_count) 
{
	if (!n) return;	
	
	ExifMnoteData* ne = (ExifMnoteData*)n;
	if (ne->methods.load != exif_mnote_data_huawei_load) return;

	if (entry_count && *entry_count) {
		mnote_huawei_free_entry_count(*entry_count);
		*entry_count = NULL;
	}

	MnoteHuaweiEntryCount* ec = exif_huawei_entry_count_new();

	unsigned int count = exif_mnote_data_huawei_count_data(ne, NULL);
	if (!count) {
		mnote_huawei_free_entry_count(ec);
		return;
	}

	ec->entries = exif_mem_alloc (ec->mem, sizeof(MnoteHuaweiEntry*) * count);
	if (!ec->entries) {
		mnote_huawei_free_entry_count(ec);
		return;
	}
	ec->size = count;

	exif_mnote_data_huawei_count_data(ne, ec);
	*entry_count = ec;
}

void mnote_huawei_free_entry_count (MnoteHuaweiEntryCount* entry_count) 
{
	exif_huawei_entry_count_free(entry_count);
}

static unsigned int
exif_mnote_data_huawei_get_id (ExifMnoteData *ne, unsigned int i) 
{
	ExifMnoteDataHuawei *n = (ExifMnoteDataHuawei *) ne;
	if (!n) return 0;

	MnoteHuaweiEntry* entry = exif_mnote_data_huawei_get_entry_by_index (n, i);
	if (!entry) return 0;

	return entry->tag;
}

static const char *
exif_mnote_data_huawei_get_name (ExifMnoteData *ne, unsigned int i) 
{
	ExifMnoteDataHuawei *n = (ExifMnoteDataHuawei *) ne;
	if (!n) return NULL;

	MnoteHuaweiEntry* entry = exif_mnote_data_huawei_get_entry_by_index (n, i);
	if (!entry) return NULL;
	
	return mnote_huawei_tag_get_name(entry->tag);
}

static const char *
exif_mnote_data_huawei_get_title (ExifMnoteData *ne, unsigned int i) 
{
	ExifMnoteDataHuawei *n = (ExifMnoteDataHuawei *) ne;
	if (!n) return NULL;

	MnoteHuaweiEntry* entry = exif_mnote_data_huawei_get_entry_by_index (n, i);
	if (!entry) return NULL;
	
	return mnote_huawei_tag_get_title(entry->tag);
}

static const char *
exif_mnote_data_huawei_get_description (ExifMnoteData *ne, unsigned int i) 
{
	ExifMnoteDataHuawei *n = (ExifMnoteDataHuawei *) ne;
	if (!n) return NULL;

	MnoteHuaweiEntry* entry = exif_mnote_data_huawei_get_entry_by_index (n, i);
	if (!entry) return NULL;
	
	return mnote_huawei_tag_get_description(entry->tag);
}

static char *
exif_mnote_data_huawei_get_value (ExifMnoteData *ne, unsigned int i, char *val, unsigned int maxlen) 
{
	ExifMnoteDataHuawei *n = (ExifMnoteDataHuawei *) ne;
	if (!n) return NULL;

	MnoteHuaweiEntry* entry = exif_mnote_data_huawei_get_entry_by_index (n, i);
	if (!entry) return NULL;

	return mnote_huawei_entry_get_value(entry, val, maxlen);
}

int
exif_mnote_data_huawei_identify (const ExifData *ed, const ExifEntry *e) 
{
	int ret = 0;

    if (!e && e->size < sizeof(HUAWEI_HEADER)) return ret;
	ret = !memcmp(e->data, HUAWEI_HEADER, 8);
	if(ret) {
		return ret;
	}

	return ret;
}

int is_huawei_md(ExifMnoteData* ne) 
{
	ExifMnoteDataHuawei *n = (ExifMnoteDataHuawei *) ne;
	if (ne && ne->methods.load == exif_mnote_data_huawei_load) {
		if (n->is_loaded) return 1;
	}
	return 0;
}

ExifMnoteData *
exif_mnote_data_huawei_new (ExifMem *mem) 
{
	ExifMnoteData *ne;

	if (!mem) return NULL;

	ne = exif_mem_alloc (mem, sizeof (ExifMnoteDataHuawei));
	if (!ne)
		return NULL;

	exif_mnote_data_construct (ne, mem);

	/* Set up function pointers */
	ne->methods.free            = exif_mnote_data_huawei_free;
	ne->methods.set_byte_order  = exif_mnote_data_huawei_set_byte_order;
	ne->methods.set_offset      = exif_mnote_data_huawei_set_offset;
	ne->methods.load            = exif_mnote_data_huawei_load;
	ne->methods.save            = exif_mnote_data_huawei_save;
	ne->methods.count           = exif_mnote_data_huawei_count;
	ne->methods.get_id          = exif_mnote_data_huawei_get_id;
	ne->methods.get_name        = exif_mnote_data_huawei_get_name;
	ne->methods.get_title       = exif_mnote_data_huawei_get_title;
	ne->methods.get_description = exif_mnote_data_huawei_get_description;
	ne->methods.get_value       = exif_mnote_data_huawei_get_value;

	return ne;
}
