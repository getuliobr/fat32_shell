/**
 *    Descrição: Estrutura de dados para implementação da FAT32
 *    Autores: Getulio Coimbra Regis, Igor Lara de Oliveira
 *    Creation Date: 30 / 06 / 2022
 * */
#include <stdint.h>

// FLAGS
#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE 0x20
#define ATTR_LONG_NAME (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)
#define ATTR_LONG_NAME_MASK (ATTR_LONG_NAME | ATTR_DIRECTORY | ATTR_ARCHIVE)

#define FREE_CLUSTER 0x00000000
#define END_OF_CHAIN 0x0FFFFFF8

// Struct de boot sector
struct boot_sector {
	uint8_t BS_jmpBoot[3];
	char BS_OEMName[8];
	uint16_t BPB_BytsPerSec;
	uint8_t BPB_SecPerClus;
	uint16_t BPB_RsvdSecCnt;
	uint8_t BPB_NumFATs;
	uint16_t BPB_RootEntCnt;
	uint16_t BPB_TotSec16;
	uint8_t BPB_Media;
	uint16_t BPB_FATSz16;
	uint16_t BPB_SecPerTrk;
	uint16_t BPB_NumHeads;
	uint32_t BPB_HiddSec;
	uint32_t BPB_TotSec32;
    // FAT 32 data
	uint32_t BPB_FATSz32;
	uint16_t BPB_ExtFlags;
	uint16_t BPB_FSVer;
	uint32_t BPB_RootClus;
	uint16_t BPB_FSInfo;
	uint16_t BPB_BkBootSec;
	uint8_t BPB_Reserved[12];
	uint8_t BS_DrvNum;
	uint8_t BS_Reserved1;
	uint8_t BS_BootSig;
	uint32_t BS_VolID;
	char BS_VolLab[11];
	char BS_FilSysType[8];
  uint8_t BS_BootCode[420];
  uint16_t BS_Signature;
}__attribute__((packed));

// Struct de FSINFO
struct FSInfo {
	uint32_t FSI_LeadSig;
	uint8_t FSI_Reserved1[480];
	uint32_t FSI_StrucSig;
	uint32_t FSI_Free_Count;
	uint32_t FSI_Nxt_Free;
	uint8_t FSI_Reserved2[12];
	uint32_t FSI_TrailSig;
}__attribute__((packed));

// Struct de ShortDirEntry
struct ShortDirEntry {
	char DIR_Name[8];
	char DIR_Extension[3];
	uint8_t DIR_Attr;
	uint8_t DIR_NTRes;
	uint8_t DIR_CrtTimeTenth;
	uint16_t DIR_CrtTime;
	uint16_t DIR_CrtDate;
	uint16_t DIR_LstAccDate;
	uint16_t DIR_FstClusHI;
	uint16_t DIR_WrtTime;
	uint16_t DIR_WrtDate;
	uint16_t DIR_FstClusLO;
	uint32_t DIR_FileSize;
}__attribute__((packed));

// Struct de Long Dir entry
struct LongDirEntry {
	uint8_t LDIR_Ord;
	uint16_t LDIR_Name1[5];
	uint8_t LDIR_Attr;
	uint8_t LDIR_Type;
	uint8_t LDIR_Chksum;
	uint16_t LDIR_Name2[6];
	uint16_t LDIR_FstClusLO;
	uint16_t LDIR_Name3[2];
}__attribute__((packed));

// Estrutura para dar opção de tratar Dir Entry como short ou long
typedef union { 
  struct ShortDirEntry short_dir;
	struct LongDirEntry long_dir;
} DirEntry;

// Struct de diretório para guardar informações
typedef struct directory {
	DirEntry* entries;
	uint32_t quantity;
	struct directory* previous;
	char name[260];
	uint32_t cluster;
} directory_t;

// Pilha de diretórios
extern directory_t* directory_stack;
// Contador da pilha
extern uint32_t directory_stack_count;

directory_t* create_directory_struct(directory_t* previous, char* name);

void read_disk(const char *disk_name);
void close_disk();

uint32_t get_fat_address(uint32_t sector);
uint64_t get_cluster_offset(uint64_t sector);
uint32_t get_cluster_info(uint64_t sector);
uint32_t get_entry_disk_position(uint32_t cluster, int entry_pos);
uint32_t allocate_clusters(uint32_t cluster_count);
uint32_t get_last_cluster_in_chain(uint32_t chain_start);

void write_in_fat(uint32_t cluster, uint32_t* value);

void info();
void read_dir();
void ls();
void cluster(int i);
void cd(char* folder);
void pwd();
void attr(char* entry_name);
void rename_dir_entry(char* entry_name, char* new_name);
void rm(char* entry_name);
void touch(char* file_name);
void mkdir(char* entry_name);
void rmdir(char* entry_name);

void create_formated_name(char* name, char* unformatted_name);
void print_name(char* name);