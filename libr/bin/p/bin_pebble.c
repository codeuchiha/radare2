/* radare - LGPL - Copyright 2014-2015 - pancake */

#include <r_types.h>
#include <r_util.h>
#include <r_lib.h>
#include <r_bin.h>

// Taken from https://pebbledev.org/wiki/Applications

#define APP_NAME_BYTES 32
#define COMPANY_NAME_BYTES 32

R_PACKED (
typedef struct  {
	ut8 major; //!< "compatibility" version number
	ut8 minor;
}) Version;

R_PACKED (
typedef struct  {
	char header[8];                   //!< Sentinal value, should always be 'PBLAPP\0\0'
	Version struct_version;           //!< version of this structure's format
	Version sdk_version;              //!< version of the SDK used to build this app
	Version app_version;              //!< version of the app
	ut16 size;                    //!< size of the app binary, including this metadata but not the reloc table
	ut32 offset;                  //!< The entry point of this executable
	ut32 crc;                     //!< CRC of the app data only, ie, not including this struct or the reloc table at the end
	char name[APP_NAME_BYTES];        //!< Name to display on the menu
	char company[COMPANY_NAME_BYTES]; //!< Name of the maker of this app
	ut32 icon_resource_id;        //!< Resource ID within this app's bank to use as a 32x32 icon
	ut32 sym_table_addr;          //!< The system will poke the sdk's symbol table address into this field on load
	ut32 flags;                   //!< Bitwise OR of PebbleAppFlags
	ut32 reloc_list_start;        //!< The offset of the address relocation list
	ut32 num_reloc_entries;       //!< The number of entries in the address relocation list
	ut8 uuid[16];
}) PebbleAppInfo;

static bool check_bytes(const ut8 *buf, ut64 length) {
	return (length > 7 && !memcmp (buf, "PBLAPP\x00\x00", 8));
}

static void * load_bytes(RBinFile *bf, const ut8 *buf, ut64 sz, ut64 loadaddr, Sdb *sdb){
	check_bytes (buf, sz);
	// XXX: this may be wrong if check_bytes is true
	return R_NOTNULL;
}

static bool load(RBinFile *bf) {
	const ut8 *bytes = bf ? r_buf_buffer (bf->buf) : NULL;
	ut64 sz = bf ? r_buf_size (bf->buf): 0;
	return check_bytes (bytes, sz);
}

static int destroy(RBinFile *bf) {
	//r_bin_pebble_free ((struct r_bin_pebble_obj_t*)bf->o->bin_obj);
	return true;
}

static ut64 baddr(RBinFile *bf) {
	return 0LL;
}

/* accelerate binary load */
static RList *strings(RBinFile *bf) {
	return NULL;
}

static RBinInfo* info(RBinFile *bf) {
	RBinInfo *ret = NULL;
	PebbleAppInfo pai;
	memset (&pai, 0, sizeof (pai));
	int reat = r_buf_read_at (bf->buf, 0, (ut8*)&pai, sizeof (pai));
	if (reat != sizeof (pai)) {
		eprintf ("Truncated Header\n");
		return NULL;
	}
	if (!(ret = R_NEW0 (RBinInfo)))
		return NULL;
	ret->lang = NULL;
	ret->file = strdup (bf->file);
	ret->type = strdup ("pebble");
	ret->bclass = r_str_ndup (pai.name, 32);
	ret->rclass = r_str_ndup (pai.company, 32);
	ret->os = strdup ("rtos");
	ret->subsystem = strdup ("pebble");
	ret->machine = strdup ("watch");
	ret->arch = strdup ("arm"); // thumb only
	ret->has_va = 1;
	ret->bits = 16;
	ret->big_endian = 0;
	ret->dbg_info = 0;
	return ret;
}

static RList* sections(RBinFile *bf) {
	ut64 textsize = UT64_MAX;
	RList *ret = NULL;
	RBinSection *ptr = NULL;
	PebbleAppInfo pai;
	memset (&pai, 0, sizeof (pai));
	if (!r_buf_read_at (bf->buf, 0, (ut8*)&pai, sizeof(pai))) {
		eprintf ("Truncated Header\n");
		return NULL;
	}
	if (!(ret = r_list_new ()))
		return NULL;
	ret->free = free;
	// TODO: load all relocs
	if (!(ptr = R_NEW0 (RBinSection)))
		return ret;
	strcpy (ptr->name, "relocs");
	ptr->vsize = ptr->size = pai.num_reloc_entries * sizeof (ut32);
	ptr->vaddr = ptr->paddr = pai.reloc_list_start;
	ptr->srwx = R_BIN_SCN_READABLE | R_BIN_SCN_WRITABLE | R_BIN_SCN_MAP;
	ptr->add = true;
	r_list_append (ret, ptr);
	if (ptr->vaddr<textsize)
		textsize = ptr->vaddr;

	// imho this must be a symbol
	if (!(ptr = R_NEW0 (RBinSection)))
		return ret;
	strcpy (ptr->name, "symtab");
	ptr->vsize = ptr->size = 0;
	ptr->vaddr = ptr->paddr = pai.sym_table_addr;
	ptr->srwx = R_BIN_SCN_READABLE | R_BIN_SCN_MAP;
	ptr->add = true;
	r_list_append (ret, ptr);
	if (ptr->vaddr<textsize)
		textsize = ptr->vaddr;

	if (!(ptr = R_NEW0 (RBinSection)))
		return ret;
	strcpy (ptr->name, "text");
	ptr->vaddr = ptr->paddr = 0x80;
	ptr->vsize = ptr->size = textsize - ptr->paddr;
	ptr->srwx = R_BIN_SCN_READABLE | R_BIN_SCN_WRITABLE |
		R_BIN_SCN_EXECUTABLE | R_BIN_SCN_MAP;
	ptr->add = true;
	r_list_append (ret, ptr);

	if (!(ptr = R_NEW0 (RBinSection)))
		return ret;
	strcpy (ptr->name, "header");
	ptr->vsize = ptr->size = sizeof (PebbleAppInfo);
	ptr->vaddr = ptr->paddr = 0;
	ptr->srwx = R_BIN_SCN_READABLE | R_BIN_SCN_MAP;
	ptr->add = true;
	r_list_append (ret, ptr);

	return ret;
}

#if 0
static RList* relocs(RBinFile *bf) {
	RList *ret = NULL;
	RBinReloc *ptr = NULL;
	ut64 got_addr;
	int i;

	if (!(ret = r_list_new ()))
		return NULL;
	ret->free = free;
	return ret;
}
#endif

static RList* entries(RBinFile *bf) {
	RBinAddr *ptr = NULL;
	RList *ret;
	PebbleAppInfo pai;
	if (!r_buf_read_at (bf->buf, 0, (ut8*)&pai, sizeof(pai))) {
		eprintf ("Truncated Header\n");
		return NULL;
	}
	if (!(ret = r_list_new ()))
		return NULL;
	ret->free = free;
	if (!(ptr = R_NEW0 (RBinAddr)))
		return ret;
	ptr->paddr = pai.offset;
	ptr->vaddr = pai.offset;
	r_list_append (ret, ptr);
	return ret;
}

struct r_bin_plugin_t r_bin_plugin_pebble = {
	.name = "pebble",
	.desc = "Pebble Watch App",
	.license = "LGPL",
	.load = &load,
	.load_bytes = &load_bytes,
	.destroy = &destroy,
	.check_bytes = &check_bytes,
	.baddr = &baddr,
	.entries = entries,
	.sections = sections,
	.strings = &strings,
	.info = &info,
	//.relocs = &relocs
};

#ifndef CORELIB
struct r_lib_struct_t radare_plugin = {
	.type = R_LIB_TYPE_BIN,
	.data = &r_bin_plugin_pebble,
	.version = R2_VERSION
};
#endif
