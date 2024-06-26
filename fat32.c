/**
 *    Descrição: Estrutura de dados para implementação da FAT32
 *    Autores: Getulio Coimbra Regis, Igor Lara de Oliveira
 *    Creation Date: 30 / 06 / 2022
 * */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <time.h>
#include <math.h>
#include "fat32.h"

// Ponteiro do disco/imagem
FILE *disk;

// Struct do boot sector
static struct boot_sector bs;

// Struct da FSINFO
static struct FSInfo fs;

// Primeiro cluster de dados
uint64_t first_data_sector;
// Offset do diretório /
uint64_t rootdir_offset;

// Stack do diretório
directory_t* directory_stack;
// Contador de diretórios na stack
uint32_t directory_stack_count;

// Flag de free cluster para escrever na FAT
uint32_t FREE_CLUSTER_POINTER = FREE_CLUSTER;
// Flag de entrada livre para escrever no arquivo/pasta
uint8_t AVAILABLE_ENTRY_POINTER = 0xE5;

// Lista de caracteres proibidos em um nome da ShortEntry
char prohibited[] = {'+', ',', ';', '=', '[', ']', '.', ' ', 0};

// Função que retorna o offset do cluster do setor passado em parâmetro
uint64_t get_cluster_offset(uint64_t sector) {
	return (((sector - 2) * bs.BPB_SecPerClus) + first_data_sector);
}

// Função que retorna endereço da FAT do setor passado em parâmetro
uint32_t get_fat_address(uint32_t sector) {
	return (bs.BPB_RsvdSecCnt * bs.BPB_BytsPerSec) + sector * sizeof(uint32_t);
}

// Função que retorna o que está escrito na FAT na posicao do setor
uint32_t get_cluster_info(uint64_t sector) {
	uint32_t fat_address = get_fat_address(sector);
	uint32_t value;
	fseek(disk, fat_address, SEEK_SET);
	fread(&value, sizeof(uint32_t), 1, disk);
	return value >= END_OF_CHAIN ? END_OF_CHAIN : value;
}

// Cria nova estrutura de diretório e retorna
directory_t* create_directory_struct(directory_t* previous, char* name){
	// Aloca um novo diretório e preenche com os dados passados por parâmetro
	directory_t* new_dir = (directory_t*)calloc(1, sizeof(directory_t));
	new_dir->previous = previous;
	new_dir->entries = NULL;
	int i;
	for(i = 0; i < 11; i++) {
		if(name[i] == '\0' || name[i] == 0x20) break;
		new_dir->name[i] = name[i];
	}
	new_dir->name[i] = '\0';
	return new_dir;
}

// Lê a imagem/disco passado por parâmetro
void read_disk(const char *disk_name) {
	// Abre o arquivo .img
	disk = fopen(disk_name, "rb+");
	// Le os primeiros bytes e coloca em uma estrutura de Boot Sector
	fread(&bs, sizeof(struct boot_sector), 1, disk);

	// Calcula a posição do FSINFO
	uint32_t fsinfo_offset = bs.BPB_BytsPerSec * bs.BPB_FSInfo;

	// Procura a posição do FSINFO e coloca em uma estrutura de FSINFO
	fseek(disk, fsinfo_offset, SEEK_SET);
	fread(&fs, sizeof(struct FSInfo), 1, disk);

	// Calcula a posição do primeiro setor de arquivos e inicia na pasta "/"
	first_data_sector = bs.BPB_RsvdSecCnt + (bs.BPB_NumFATs * bs.BPB_FATSz32);
	rootdir_offset = get_cluster_offset(bs.BPB_RootClus) * bs.BPB_BytsPerSec;

	directory_stack_count = 0;
	directory_stack = create_directory_struct(NULL, "/");
	directory_stack->cluster = bs.BPB_RootClus;

	// Lê o diretorio "/"
	read_dir();
}

// Imprime as informações da FAT
void info() {
	printf("FAT Filesystem information\n\n");
	printf("OEM name: %s\n", bs.BS_OEMName);
	printf("Total sectors: %d\n", bs.BPB_TotSec32);
	printf("Jump: 0x%X%X%X\n", bs.BS_jmpBoot[0], bs.BS_jmpBoot[1], bs.BS_jmpBoot[2]);
	printf("Sector size: %d\n", bs.BPB_BytsPerSec);	
	printf("Sectors per cluster: %d\n", bs.BPB_SecPerClus);
	printf("Reserved sectors: %d\n", bs.BPB_RsvdSecCnt);
	printf("Number of fats: %d\n", bs.BPB_NumFATs);
	printf("Root dir entries: %d\n", bs.BPB_RootEntCnt);
	printf("Media: 0x%X\n", bs.BPB_Media);
	printf("Sectors by FAT: %d\n", bs.BPB_FATSz32);
	printf("Sectors per track: %d\n", bs.BPB_SecPerTrk);
	printf("Number of heads: %d\n", bs.BPB_NumHeads);
	printf("Hidden sectors: %d\n", bs.BPB_HiddSec);
  printf("Drive number: 0x%02X\n", bs.BS_DrvNum);
  printf("Current head: 0x%02X\n", bs.BS_Reserved1);
  printf("Boot signature: 0x%02X\n", bs.BS_BootSig);
  printf("Volume ID: 0x%08X\n", bs.BS_VolID);
  printf("Volume label: ");
  for(int i = 0; i < 11; i++) {
    printf("%c", bs.BS_VolLab[i]);
  }
  printf("\n");
  printf("Filesystem type: ");
  for(int i = 0; i < 8; i++) {
    printf("%c", bs.BS_FilSysType[i]);
  }
  printf("\n");
  printf("BS Signature: 0x%04X\n", bs.BS_Signature);

	uint64_t fat1_address = bs.BPB_RsvdSecCnt * bs.BPB_BytsPerSec;
	uint64_t fat2_address = (bs.BPB_RsvdSecCnt + bs.BPB_FATSz32) * bs.BPB_BytsPerSec;

  printf("FAT1 start address: 0x%016lX\n", fat1_address);
  printf("FAT2 start address: 0x%016lX\n", fat2_address);
  printf("Data start address: 0x%016lX\n", rootdir_offset);
}

// Coloca todas as entradas de diretorios de uma pasta
void read_dir() {
	uint32_t next_cluster = directory_stack->cluster;
	uint32_t currsec;


	if(directory_stack->entries != NULL) free(directory_stack->entries);

	int max_dir_entries = 0;
	int max_dir_entry_per_cluster = bs.BPB_BytsPerSec * bs.BPB_SecPerClus / sizeof(DirEntry);


	while (next_cluster != END_OF_CHAIN) {
		currsec = get_cluster_offset(next_cluster);
		
		int old_max_dir_entries = max_dir_entries;
		max_dir_entries += max_dir_entry_per_cluster;

		DirEntry* new_dir_entries = (DirEntry*) malloc(max_dir_entries * sizeof(DirEntry));
		memcpy(new_dir_entries, directory_stack->entries, old_max_dir_entries * sizeof(DirEntry));
		free(directory_stack->entries);

		directory_stack->entries = new_dir_entries;

    fseek(disk, currsec * bs.BPB_SecPerClus * bs.BPB_BytsPerSec, SEEK_SET);
    fread(&directory_stack->entries[old_max_dir_entries], sizeof(DirEntry), max_dir_entry_per_cluster, disk);

		next_cluster = get_cluster_info(next_cluster);
	}

	directory_stack->quantity = max_dir_entries;
}

// Função para Imprimir a data do sistema com os calculos já feitos
void print_date(uint16_t date) {
	uint16_t day = date & 0b11111;
	uint16_t month = (date >> 5) & 0b1111;
	uint16_t year = 1980 + ((date >> 9) & 0b1111111);

	printf("%02d/%02d/%d", day, month, year);
}

// Função para Imprimir o horário no sistema com os calculos já feitos
void print_time(uint16_t time) {
	uint16_t seconds = (time & 0b11111) << 1;
	uint16_t minutes = (time >> 5) & 0b111111;
	uint16_t hour = ((time >> 11) & 0b11111);

	printf("%02d:%02d:%02d", hour, minutes, seconds);
}

// Comando ls para listar arquivos/pastas da pasta atual
void ls() {
	printf("CREATEDATE CRT_TIME UPDATEDATE UPD_TIME LSTACCDATE SIZE\t\tNAME\n");
	// Procura todos os diretorios encontrados na pasta atual através da quantidade de diretorios dentro da pilha da estrutura Dir_stack
	for(int i = 0; i < directory_stack->quantity; i++) {
		uint8_t status_byte = directory_stack->entries[i].short_dir.DIR_Name[0];
		if(status_byte == 0x00) break; 
		if(status_byte == 0xE5) continue;
		if((directory_stack->entries[i].short_dir.DIR_Attr & ATTR_LONG_NAME_MASK) == ATTR_LONG_NAME) continue;

		// Imprime as informações do arquivo
		print_date(directory_stack->entries[i].short_dir.DIR_CrtDate);
		printf(" ");
		print_time(directory_stack->entries[i].short_dir.DIR_CrtTime);
		printf(" ");
		print_date(directory_stack->entries[i].short_dir.DIR_WrtDate);
		printf(" ");
		print_time(directory_stack->entries[i].short_dir.DIR_WrtTime);
		printf(" ");
		print_date(directory_stack->entries[i].short_dir.DIR_LstAccDate);
		printf(" %u\t\t", directory_stack->entries[i].short_dir.DIR_FileSize);

		// Verifica se é um diretorio e imprime no ls
		int is_directory = (directory_stack->entries[i].short_dir.DIR_Attr & (ATTR_DIRECTORY | ATTR_VOLUME_ID)) == ATTR_DIRECTORY ? 1 : 0;
		if(is_directory) printf("d ");
		else printf("- ");

		print_name(directory_stack->entries[i].short_dir.DIR_Name);

		printf("\n");
	}
}

// Exibe informação do cluster com posição passado por parâmetro
void cluster(int i) {
  // Procura a posição do cluster
	fseek(disk, i * bs.BPB_SecPerClus * bs.BPB_BytsPerSec, SEEK_SET);
	uint32_t cluster_size = bs.BPB_SecPerClus * bs.BPB_BytsPerSec;
	uint8_t* cluster_data = (uint8_t*) malloc(cluster_size);
  // Grava as informações do cluster no cluster_data
	fread(cluster_data, 1, cluster_size, disk);

	int colunas = 16;


  // Faz a formatação e imprime as informações do cluster
	for(int linha = 0; linha < cluster_size / colunas; linha++) {
		for(int coluna = 0; coluna < colunas; coluna++)
			printf("%02X ", cluster_data[linha*colunas + coluna]);
		printf("   ");
		for(int coluna = 0; coluna < colunas; coluna++) {
			int currChar = linha*colunas + coluna;
			cluster_data[currChar] = cluster_data[currChar] != '\n' ? cluster_data[currChar] : ' ';
			cluster_data[currChar] = cluster_data[currChar] != 0x08 ? cluster_data[currChar] : ' ';
			cluster_data[currChar] = cluster_data[currChar] != 0x09 ? cluster_data[currChar] : ' ';
			cluster_data[currChar] = cluster_data[currChar] != 0x0A ? cluster_data[currChar] : ' ';
			cluster_data[currChar] = cluster_data[currChar] != 0x0B ? cluster_data[currChar] : ' ';
			cluster_data[currChar] = cluster_data[currChar] != 0x0C ? cluster_data[currChar] : ' ';
			cluster_data[currChar] = cluster_data[currChar] != 0x0D ? cluster_data[currChar] : ' ';
			cluster_data[currChar] = cluster_data[currChar] != 0 ? cluster_data[currChar] : '.';
			printf("%c", cluster_data[currChar]);
		}
		printf("\n");
	}
	free(cluster_data);
}

// Navegar entre pastas e usado em outros lugares entao criamos esse wrapper para poder ser utilizado por outras funcoes
// Retorna 1 se conseguiu navegar ate a pasta ou 0 se nao conseguiu
int cd_wrapper(char* folder, char* command) {
	// Se a pasta for a atual não faz nada
	if(!strcmp(folder, ".")) {
		return 1;
	}

	// Se a pasta for a pasta pai, volta para a pasta pai
	if(!strcmp(folder, "..")) {
		if(directory_stack_count == 0) return 1;
		directory_t* old_directory = directory_stack;
		directory_stack = directory_stack->previous;
		directory_stack_count--;
		free(old_directory);
		read_dir();
		return 1;
	}

  // Converte o nome de entrada da pasta na função
	char folder_name[11];
	create_formated_name(folder_name, folder);
  // Verifica a validade do nome
	if(!folder_name[0]) {
		printf("%s: %s: Invalid folder name\n", command, folder);
		return 0;
	}

  // Procura o diretório
	for(int i = 0; i < directory_stack->quantity; i++) {
		uint8_t status_byte = directory_stack->entries[i].short_dir.DIR_Name[0];
    // Se status_byte == 0 significa que acabou as dir_entry
		if(status_byte == 0x00) break;
    // Se status_byte == E5 significa que o espaço está livre
		if(status_byte == 0xE5) continue;
    // Se for dir de long name ignora
		if((directory_stack->entries[i].short_dir.DIR_Attr & ATTR_LONG_NAME_MASK) == ATTR_LONG_NAME) continue;
    // Se for arquivo ignora
		if((directory_stack->entries[i].short_dir.DIR_Attr & ATTR_DIRECTORY) != ATTR_DIRECTORY) continue;

    // Se acha o diretório, muda a stack para ele
		if(!memcmp(folder_name, directory_stack->entries[i].short_dir.DIR_Name, 11)) {
			directory_t* new_directory = create_directory_struct(directory_stack, directory_stack->entries[i].short_dir.DIR_Name);
			new_directory->cluster = (directory_stack->entries[i].short_dir.DIR_FstClusHI<<16) | directory_stack->entries[i].short_dir.DIR_FstClusLO;
			directory_stack = new_directory;
			directory_stack_count++;
			read_dir();
			return 1;
		}
	}
  // Se não achar, retorna erro
	printf("%s: %s: No such directory\n", command, folder);
	return 0;
}

// Comando do CD
void cd(char* folder) {
	// Usa o wrapper da funcao anterior
	cd_wrapper(folder, "cd");
}

// Função que formata o nome da entrada recebida
void create_formated_name(char* name, char* unformatted_name) {
	name[0] = 0;
  int prohibitedLen = strlen(prohibited);

	int dot_pos = -1; // -1 significa que não possui ponto no nome não formatado
  // Procura a posição do ponto no nome pré formatado, se existir
	for(int i = 0; i < strlen(unformatted_name); i++) {
		if(unformatted_name[i] == '.' && dot_pos != -1) {
			return;
		}

		if(unformatted_name[i] == '.' && dot_pos == -1) {
			dot_pos = i;
		}

		if(dot_pos == 0)
			return;
	}

  // Se o nome não formatado for maior que 13 retorna da função
	if(strlen(unformatted_name) > 13)
		return;

  // Se o nome não formatado for igual a 13 e não existir ponto no nome, retorna da função 
	if(strlen(unformatted_name) == 13 && dot_pos == -1)
		return;

  // Se o nome não formatado possuir ponto no nome e sua extensão for maior que 3, retorna da função
	if(strlen(unformatted_name) - (dot_pos + 1) > 3 && dot_pos != -1)
		return;


  // Se o nome possui ponto 
	if(dot_pos != -1) {
    // Copia parte do nome antes do ponto
		for(int i = 0; i < dot_pos; i++) {
			name[i] = toupper(unformatted_name[i]);
      for(int j = 0; j < prohibitedLen; j++) {
        if(name[i] == prohibited[j]) {
          name[i] = '_';
          break;
        }
		  }
    }
    // Troca '.' por ' '
		for(int i = dot_pos; i < 8; i++)
			name[i] = 0x20;
		int j = 8;
    // Continua copiando o nome
		for(int i = dot_pos + 1; i < strlen(unformatted_name); i++, j++)
			name[j] = toupper(unformatted_name[i]);
    // Se não completou o espaço de 11 caracteres, preenche com ' '
		for(;j < 11; j++)
			name[j] = 0x20;

  // Se o nome não possui ponto, apenas copia o novo nome e preenche com ' '
	} else {
		for(int i = 0; i < strlen(unformatted_name); i++)
			name[i] = toupper(unformatted_name[i]);
		for(int i = strlen(unformatted_name); i < 11; i++)
			name[i] = 0x20;
	}
}

// Faz a impressão do nome
void print_name(char* name) {
	for(int i = 0; i < 8; i++) {
		if(name[i] == 0x20) break;
		printf("%c", name[i]);
	}

	if(name[8] == 0x20 || name[8] == 0x00) return;
	
	printf(".");
	for(int i = 8; i < 11; i++) {
		if(name[i] == 20) break;
		printf("%c", name[i]);
	}
}

// PWD recursivo
void pwd_r(int pos, directory_t* curr) {
  // Procura recursivamente até a pasta raiz e vai imprimindo na tela a pasta que está passando atualmente
	if(pos != 0) {
		pwd_r(pos-1, curr->previous);
		printf("/");
		print_name(curr->name);
	} 
}

// Imprime o diretório atual
void pwd() {
	if(directory_stack_count) pwd_r(directory_stack_count, directory_stack);
	else printf("/");
	printf("\n");
}

// Imprime as informações do arquivo/diretorio
void attr(char* entry_name) {
  //Cria nome formatado
	char name[11];
	create_formated_name(name, entry_name);
	if(!name[0]) {
		printf("attr: %s: Invalid file name\n", entry_name);
		return;
	}

  // Procura por arquivo/diretório no diretório atual
	for(int i = 0; i < directory_stack->quantity; i++) {
		uint8_t status_byte = directory_stack->entries[i].short_dir.DIR_Name[0];
		// Se status_byte == 0 significa que acabou as dir_entry
		if(status_byte == 0x00) break;
    // Se status_byte == E5 significa que o espaço está livre
		if(status_byte == 0xE5) continue;
    // Se for dir de long name ignora
		if((directory_stack->entries[i].short_dir.DIR_Attr & ATTR_LONG_NAME_MASK) == ATTR_LONG_NAME) continue;
    // Se não for o arquivo/diretório, ignora
		if(memcmp(name, directory_stack->entries[i].short_dir.DIR_Name, 11)) continue;

    // Imprime as informações do arquivo/diretório
		DirEntry file  = directory_stack->entries[i];
		printf("Name = ");
		for (int i = 0; i < 8; i++) {
			printf("%c", file.short_dir.DIR_Name[i]);
		}
		printf("\n");
		
		printf("Extension = ");
		for (int i = 0; i < 3; i++) {
			printf("%c", file.short_dir.DIR_Extension[i]);
		}
		printf("\n");
		
		printf("ATTR_READ_ONLY = %d\n", file.short_dir.DIR_Attr & ATTR_READ_ONLY ? 1 : 0);
		printf("ATTR_HIDDEN = %d\n", file.short_dir.DIR_Attr & ATTR_HIDDEN ? 1 : 0);
		printf("ATTR_SYSTEM = %d\n", file.short_dir.DIR_Attr & ATTR_SYSTEM ? 1 : 0);
		printf("ATTR_VOLUME_ID = %d\n", file.short_dir.DIR_Attr & ATTR_VOLUME_ID ? 1 : 0);
		printf("ATTR_DIRECTORY = %d\n", file.short_dir.DIR_Attr & ATTR_DIRECTORY ? 1 : 0);
		printf("ATTR_ARCHIVE = %d\n", file.short_dir.DIR_Attr & ATTR_ARCHIVE ? 1 : 0);
		printf("NTRes = %d\n", file.short_dir.DIR_NTRes);
		printf("CRt Time Tenth = %d\n", file.short_dir.DIR_CrtTimeTenth);
		printf("Crt Time = ");
		print_time(file.short_dir.DIR_CrtTime);
		printf("\n");
		printf("Crt Date = ");
		print_date(file.short_dir.DIR_CrtDate);
		printf("\n");
		printf("Lst Acc Date = ");
		print_date(file.short_dir.DIR_LstAccDate);
		printf("\n");
		printf("Fst Clus HI = %d\n", file.short_dir.DIR_FstClusHI);
		printf("Wrt Time = ");
		print_time(file.short_dir.DIR_WrtTime);
		printf("\n");
		printf("Wrt Date = ");
		print_date(file.short_dir.DIR_WrtDate);
		printf("\n");
		printf("Fst Clus LO = %d\n", file.short_dir.DIR_FstClusLO);
		printf("File Size = %u bytes\n", file.short_dir.DIR_FileSize);
		return;
	}

	printf("attr: %s: No such file or directory\n",entry_name);
}

// Pega a posição de entrada no disco
uint32_t get_entry_disk_position(uint32_t cluster, int entry_pos) {
  // Calcula offset partindo do cluster inicial para a posição
	uint32_t offset_from_cluster_begining = (entry_pos  * sizeof(DirEntry))  % (bs.BPB_BytsPerSec * bs.BPB_SecPerClus);
  // Calcula o cluster que está o dir_entry
	uint32_t file_cluster = (entry_pos  * sizeof(DirEntry)) / (bs.BPB_BytsPerSec * bs.BPB_SecPerClus);

	for(int i = 0; i < file_cluster; i++)
		cluster = get_cluster_info(cluster);

  // Retorna a posição de entrada no disco
	return get_cluster_offset(cluster) * bs.BPB_SecPerClus * bs.BPB_BytsPerSec + offset_from_cluster_begining;
}

// Renomeia o arquivo/diretório
void rename_dir_entry(char* entry_name, char* new_name) {
	char old_entry_name[11];
  // Formata o nome antigo
	create_formated_name(old_entry_name, entry_name);
  // Verifica se nome é valido
	if(!old_entry_name[0]) {
		printf("rename: %s: Invalid entry name\n", entry_name);
		return;
	}
		
	char new_entry_name[11];
  // Formata o novo nome
	create_formated_name(new_entry_name, new_name);
  // Verifica se nome é valido
	if(!new_entry_name[0]) {
		printf("rename: %s: Invalid new name\n", new_name);
		return;
	}

  // Se os nomes são iguais, retorna
	if(!memcmp(old_entry_name, new_entry_name, 11)) return;

	int entry_pos = -1;

  // Procura por diretório que possui o mesmo nome
	for(int i = 0; i < directory_stack->quantity; i++) {
		uint8_t status_byte = directory_stack->entries[i].short_dir.DIR_Name[0];
    // Se status_byte == E5 significa que o espaço está livre
		if(status_byte == 0xE5) continue;
    // Se for dir de long name ignora
		if((directory_stack->entries[i].short_dir.DIR_Attr & ATTR_LONG_NAME_MASK) == ATTR_LONG_NAME) continue;
		
    // Se acabou de ler o diretório e não encontrou o diretório antigo, retorna com erro
		if(status_byte == 0x00 && entry_pos == -1) {
			printf("rename: '%s': No such file or directory\n", entry_name);
			return;
		};

    // Se achou o diretório antigo guarda a informação da posição dele
		if(!memcmp(old_entry_name, directory_stack->entries[i].short_dir.DIR_Name, 11)) {
			entry_pos = i;
		};

    // Se o novo nome já existir, retorna com erro
		if(!memcmp(new_entry_name, directory_stack->entries[i].short_dir.DIR_Name, 11)) {
			printf("rename: '%s': Already exists\n", new_name);
			return;
		}
	}

  // Pega a data atual do computador
  time_t t = time(NULL);
  struct tm *tm = localtime(&t);

  // Converte a data em binário
  uint16_t date = 0;
  date |= tm->tm_mday;
  date |= (tm->tm_mon + 1) << 5;
  date |= (tm->tm_year - 80) << 9;

  // Converte hora em binário
  uint16_t time = 0;
  tm->tm_sec = tm->tm_sec >= 58 ? 58 : tm->tm_sec;
  time |= (tm->tm_sec) >> 1;
  time |= (tm->tm_min << 5);
  time |= (tm->tm_hour << 11);

  // Atualiza nome e data de escrita do arquivo
	memcpy(directory_stack->entries[entry_pos].short_dir.DIR_Name, new_entry_name, 11);
  directory_stack->entries[entry_pos].short_dir.DIR_WrtDate = date;
  directory_stack->entries[entry_pos].short_dir.DIR_WrtTime = time;

  // Copia para a memória
  fseek(disk, get_entry_disk_position(directory_stack->cluster, entry_pos), SEEK_SET);
	fwrite(&directory_stack->entries[entry_pos], sizeof(DirEntry), 1, disk);

}

// Remove o diretóro com entry_name e, se existir, com flag de verificação se é pasta
void rm_wrapped(char* entry_name, int is_folder) {
	char rm_entry_name[11];
  // Cria nome formatado da entry_name
	create_formated_name(rm_entry_name, entry_name);
  
  // Se entry_name está inválido retorna com erro
	if(!rm_entry_name[0]) {
		if(is_folder) printf("rmdir");
		else printf("rm");
		printf(": %s: Invalid entry name\n", entry_name);
		return;
	}

  // Procura entrada com mesmo nome no diretório
	for(int entry_pos = 0; entry_pos < directory_stack->quantity; entry_pos++) {
		uint8_t status_byte = directory_stack->entries[entry_pos].short_dir.DIR_Name[0];
    // Se status_byte == E5 significa que o espaço está livre
		if(status_byte == 0xE5) continue;
    // Se for dir de long name ignora
		if((directory_stack->entries[entry_pos].short_dir.DIR_Attr & ATTR_LONG_NAME_MASK) == ATTR_LONG_NAME) continue;
		
    // Se não possui mais entradas no diretório, retorna com erro
		if(status_byte == 0x00) {
			if(is_folder) printf("rmdir");
			else printf("rm");
			printf(": '%s': No such file\n", entry_name);
			return;
		};

    // Se achou o arquivo e 
		if(!memcmp(rm_entry_name, directory_stack->entries[entry_pos].short_dir.DIR_Name, 11)) {
      // Se não existir flag de pasta e estar tentando remover uma, retorna com erro
			if((directory_stack->entries[entry_pos].short_dir.DIR_Attr & ATTR_DIRECTORY) == ATTR_DIRECTORY && !is_folder) {
				printf("rm: '%s': Can't remove a folder\n", entry_name);
				return;
			};
      // Se existir flag de pasta e estar tentando remover um arquivo, retorna com erro
			if((directory_stack->entries[entry_pos].short_dir.DIR_Attr & ATTR_DIRECTORY) != ATTR_DIRECTORY && is_folder) {
				printf("rmdir: '%s': Can't remove a file\n", entry_name);
				return;
			};

      // Limpa o ponteiro da pasta e marca como livre
			directory_stack->entries[entry_pos].short_dir.DIR_Name[0] = AVAILABLE_ENTRY_POINTER;

      // Procura a posição da dir_entry para atualização na memória
			fseek(disk, get_entry_disk_position(directory_stack->cluster, entry_pos), SEEK_SET);
      // Marca arquivo/pasta como livre
			fwrite(&AVAILABLE_ENTRY_POINTER, 1, 1, disk);

			uint32_t next_cluster = 0;
      uint32_t curr_cluster = (directory_stack->entries[entry_pos].short_dir.DIR_FstClusHI<<16) | directory_stack->entries[entry_pos].short_dir.DIR_FstClusLO;
			// Vai andando na cadeia da FAT e marcando como livre
			while (next_cluster != END_OF_CHAIN) {
				next_cluster = get_cluster_info(curr_cluster);
				write_in_fat(curr_cluster, &FREE_CLUSTER_POINTER);
				curr_cluster = next_cluster;
			}

			return;
		};
	}
}

// Chama a função de remover genérica passando flag de arquivo
void rm(char* entry_name) {
	rm_wrapped(entry_name, 0);
}

// Chama a função de remover genérica passando a flag de diretório
void rmdir(char* entry_name) {
	char rm_entry_name[11];

  //Cria nome formatado e valida o mesmo
	create_formated_name(rm_entry_name, entry_name);
	if(!rm_entry_name[0]) {
		printf("rmdir: %s: Invalid entry name\n", entry_name);
		return;
	}

	// Procura entrada com mesmo nome no diretório
	for(int entry_pos = 0; entry_pos < directory_stack->quantity; entry_pos++) {
		uint8_t status_byte = directory_stack->entries[entry_pos].short_dir.DIR_Name[0];
    // Se status_byte == E5 significa que o espaço está livre
		if(status_byte == 0xE5) continue;
    // Se for dir de long name ignora
		if((directory_stack->entries[entry_pos].short_dir.DIR_Attr & ATTR_LONG_NAME_MASK) == ATTR_LONG_NAME) continue;
		
    // Se não existir diretório com o nome e não existir mais entradas, retorna com erro
		if(status_byte == 0x00) {
			printf("rmdir: '%s': No such file\n", entry_name);
			return;
		};

    // Se a entrada da fun
		if(!memcmp(rm_entry_name, directory_stack->entries[entry_pos].short_dir.DIR_Name, 11)) {
			if((directory_stack->entries[entry_pos].short_dir.DIR_Attr & ATTR_DIRECTORY) != ATTR_DIRECTORY) {
				printf("rmdir: '%s': Can't remove a file\n", entry_name);
				return;
			};

			// Contar quantas entries estão num diretório
			cd(entry_name);
			int total_entries = 0;
			for(int entry = 0; entry < directory_stack->quantity; entry++) {
				uint8_t subdir_entry_status_byte = directory_stack->entries[entry].short_dir.DIR_Name[0];
        // Se status_byte == 0 significa que acabou as dir_entry
        if(subdir_entry_status_byte == 0x00) break;
        // Se status_byte == E5 significa que o espaço está livre
        if(subdir_entry_status_byte == 0xE5) continue;
        // Se for dir de long name ignora
        if((directory_stack->entries[entry].short_dir.DIR_Attr & ATTR_LONG_NAME_MASK) == ATTR_LONG_NAME) continue;
				
				
				// Só pode ter duas entradas no diretório (. e ..)
				if(++total_entries > 2) {
					printf("rmdir: '%s': Directory not empty\n", entry_name);
					cd("..");
					return;
				}
			};
			
			cd("..");

			break;
		};
	}

	rm_wrapped(entry_name, 1);
}

// Funcao recursiva que aloca na tabela FAT o espaco preciso
uint32_t allocate_clusters_wrapped(uint32_t cluster_count, uint32_t last_cluster) {
	uint32_t status;

	// Calcula a quantidade de endereços da fat
	uint32_t fat_addresses = bs.BPB_TotSec32 - (bs.BPB_RsvdSecCnt + (2 * bs.BPB_FATSz32));

	// Procura entre os clusters da fat o cluster que está livre
	for(uint32_t i = last_cluster; i < fat_addresses; i++) {
		status = get_cluster_info(i);
		
		// Se encontra o cluster livre, ele marca a posicao do proximo cluster 
		//que ele achar recursivamente ou marca como fim de cadeia
		if(status == FREE_CLUSTER) {
			uint32_t next_in_chain = cluster_count-1 ? allocate_clusters_wrapped(cluster_count-1, i+1) : END_OF_CHAIN;
      write_in_fat(i, &next_in_chain);
			return i;
		}
	}
  return FREE_CLUSTER;
}

// Chama a função para alocar clusters
uint32_t allocate_clusters(uint32_t cluster_count) {
	return allocate_clusters_wrapped(cluster_count, 2);
}

// Procura o último cluster na cadeia
uint32_t get_last_cluster_in_chain(uint32_t chain_start) {
	// Pega o cluster inicial e utiliza para busca
	uint32_t next_cluster = get_cluster_info(chain_start);
	uint32_t curr_cluster = chain_start;
	// Procura o cluster final através da flag END_OF_CHAIN
	while (next_cluster != END_OF_CHAIN) {
    curr_cluster = next_cluster;
    next_cluster = get_cluster_info(next_cluster);
  }
  // Retorna último cluster
  return curr_cluster;
}

// Função genérica para criaçao de arquivos/diretórios com parametro de nome do arquivo/pasta e flag
int touch_wrapper(char* file_name, uint8_t attr, DirEntry* created_entry) {
	
  // Procura o comando certo para tratar erros de acordo com a flag passada
	char command_name[6] = { 0 };
	if(created_entry != NULL) strcpy(command_name, "mv");
	else if(attr == ATTR_ARCHIVE) strcpy(command_name, "touch");
	else if(attr == ATTR_DIRECTORY) strcpy(command_name, "mkdir");
	else return 0;

	char new_entry_name[11];
  // Cria nome formatado da entrada e valida
	create_formated_name(new_entry_name, file_name);
	if(!new_entry_name[0]) {
		printf("%s: %s: Invalid name\n", command_name, file_name);
		return 0;
	}

	int entry_pos = -1;

  // Procura nos diretórios do diretório atual
	for(int i = 0; i < directory_stack->quantity; i++) {
		uint8_t status_byte = directory_stack->entries[i].short_dir.DIR_Name[0];
    // Se for dir de long name, ignora
		if((directory_stack->entries[i].short_dir.DIR_Attr & ATTR_LONG_NAME_MASK) == ATTR_LONG_NAME) continue;
		
    // Se não existe mais entradas, sai do for
		if(status_byte == 0x00) {
			if(entry_pos == -1) entry_pos = i;
			break;
		};

    // Se achou espaço livre no diretório, guarda a posição e continua a busca
		if(status_byte == 0xE5 && entry_pos == -1) {
			entry_pos = i;
			continue;
		}

    // Se já existe arquivo/diretório com nome, retorna com erro
		if(!memcmp(new_entry_name, directory_stack->entries[i].short_dir.DIR_Name, 11)) {
			printf("%s: '%s': Already exists\n", command_name, file_name);
			return 0;
		}
	}
  
	uint32_t new_entry_cluster;
	
	if(created_entry == NULL) {
		// Procura novo cluster para alocar
		new_entry_cluster = allocate_clusters(1);

		// Se cluster não possuir local para alocar, retorna com erro
		if(new_entry_cluster == FREE_CLUSTER) {
			printf("%s: '%s': Unable to alocate new cluster, disk is full?\n", command_name, file_name);
			return 0;
		}
	}

  // Se não tiver espaço, aloca novo cluster na fat
	if(entry_pos == -1) {
	// Pega o último cluster na cadeia
    uint32_t last_cluster_currfolder = get_last_cluster_in_chain(directory_stack->cluster);
    uint32_t extra_entries_start = allocate_clusters(1);

    entry_pos = directory_stack->quantity;

		// Se não conseguir alocar mais um cluster para o diretório retorna um erro e libera o cluster alocado pelo arquivo gerado do touch.
    if(extra_entries_start == FREE_CLUSTER) {
			printf("%s: '%s': Unable to alocate new cluster, disk is full?\n", command_name, file_name);
			if(created_entry == NULL) {
				uint32_t address_to_free_cluster = get_cluster_offset(new_entry_cluster);
				uint32_t free = FREE_CLUSTER;
				write_in_fat(address_to_free_cluster, &free);
			}
      return 0;
    }

    write_in_fat(last_cluster_currfolder, &extra_entries_start);

    read_dir();
	}

  // Pega a data atual do computador
  time_t t = time(NULL);
  struct tm *tm = localtime(&t);

  // Converte a data em binário
  uint16_t date = 0;
  date |= tm->tm_mday;
  date |= (tm->tm_mon + 1) << 5;
  date |= (tm->tm_year - 80) << 9;

  // Converte hora em binário
  uint16_t time = 0;
  tm->tm_sec = tm->tm_sec >= 58 ? 58 : tm->tm_sec;
  time |= (tm->tm_sec) >> 1;
  time |= (tm->tm_min << 5);
  time |= (tm->tm_hour << 11);

  // Atualiza parâmetros do arquivo
	memset(&directory_stack->entries[entry_pos], 0, sizeof(DirEntry));
  memcpy(directory_stack->entries[entry_pos].short_dir.DIR_Name, new_entry_name, 11);
	if(created_entry == NULL) {
		directory_stack->entries[entry_pos].short_dir.DIR_FstClusLO = new_entry_cluster & 0x0000FFFF;
		directory_stack->entries[entry_pos].short_dir.DIR_FstClusHI = (new_entry_cluster & 0xFFFF0000) >> 16;
		directory_stack->entries[entry_pos].short_dir.DIR_FileSize = 0;
		directory_stack->entries[entry_pos].short_dir.DIR_Attr = attr;
		directory_stack->entries[entry_pos].short_dir.DIR_CrtDate = date;
		directory_stack->entries[entry_pos].short_dir.DIR_CrtTime = time;
		directory_stack->entries[entry_pos].short_dir.DIR_WrtDate = date;
		directory_stack->entries[entry_pos].short_dir.DIR_WrtTime = time;
		directory_stack->entries[entry_pos].short_dir.DIR_LstAccDate = date;
	} else {
		memcpy(&directory_stack->entries[entry_pos], created_entry, sizeof(DirEntry));
	}

  // Coloca na memória o novo arquivo
  fseek(disk, get_entry_disk_position(directory_stack->cluster, entry_pos), SEEK_SET);
	fwrite(&directory_stack->entries[entry_pos], sizeof(DirEntry), 1, disk);

  // Se flag de diretório
	if(attr == ATTR_DIRECTORY) {
		char dot[] = {'.', 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
		char dotdot[] = {'.', '.', 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};

    // Copia dados para a memória do dir .
		DirEntry dotEntry = { 0 };
		memcpy(&dotEntry.short_dir.DIR_Name, dot, 11);
		dotEntry.short_dir.DIR_FstClusLO = new_entry_cluster & 0x0000FFFF;
		dotEntry.short_dir.DIR_FstClusHI = (new_entry_cluster & 0xFFFF0000) >> 16;
		dotEntry.short_dir.DIR_Attr = attr;
		dotEntry.short_dir.DIR_CrtDate = date;
		dotEntry.short_dir.DIR_CrtTime = time;
		dotEntry.short_dir.DIR_WrtDate = date;
		dotEntry.short_dir.DIR_WrtTime = time;
		dotEntry.short_dir.DIR_LstAccDate = date;

    // Copia dados para a memória do dir ..
		DirEntry dotdotEntry = { 0 };
		memcpy(&dotdotEntry.short_dir.DIR_Name, dotdot, 11);
		dotdotEntry.short_dir.DIR_FstClusLO = directory_stack->cluster & 0x0000FFFF;
		dotdotEntry.short_dir.DIR_FstClusHI = (directory_stack->cluster & 0xFFFF0000) >> 16;
		dotdotEntry.short_dir.DIR_Attr = attr;
		dotdotEntry.short_dir.DIR_CrtDate = date;
		dotdotEntry.short_dir.DIR_CrtTime = time;
		dotdotEntry.short_dir.DIR_WrtDate = date;
		dotdotEntry.short_dir.DIR_WrtTime = time;
		dotdotEntry.short_dir.DIR_LstAccDate = date;

    // Escreve os dados da memória dos dir's '.' e '..'
		uint64_t write_dotpos = get_cluster_offset(new_entry_cluster) * bs.BPB_SecPerClus * bs.BPB_BytsPerSec;
		fseek(disk, write_dotpos, SEEK_SET);
		fwrite(&dotEntry, sizeof(dotEntry), 1, disk);
		fwrite(&dotdotEntry, sizeof(dotEntry), 1, disk);
	}
	return 1;
}

// Chama função genérica de criação de dir_entry com flag de arquivo
void touch(char* file_name) {
	touch_wrapper(file_name, ATTR_ARCHIVE, NULL);
}

// Função que escreve valores na FAT
void write_in_fat(uint32_t cluster, uint32_t* value) {
	uint32_t fat_address = get_fat_address(cluster);
	uint32_t fat2_address = fat_address + bs.BPB_FATSz32 * bs.BPB_BytsPerSec;

	fseek(disk, fat_address, SEEK_SET);
	fwrite(value, sizeof(uint32_t), 1, disk);
	fseek(disk, fat2_address, SEEK_SET);
	fwrite(value, sizeof(uint32_t), 1, disk);
}

// Chama função genérica de criação de dir_entry com flag de diretório
void mkdir(char* entry_name) {
	touch_wrapper(entry_name, ATTR_DIRECTORY, NULL);
}

/*

Como comentado no relatório na parte de Resultados e Discussão nós desistimos de implementar o CP e o MV, 
o código abaixo é referente ao MV não chegamos a fazer o CP. 

// Seguindo a documentação da Microsoft converter um nome de longentry para um nome de shortentry
void basis_name_generator(char* name, char* output) {
	int i, j;

	int prohibitedLen = strlen(prohibited);

	int len = strlen(name);
	int dotPosition = 0;

	for(i = 0; i < 11; i++)
		output[i] = 0x20;

	// Transforma todo caracter para maiusculo e transforma os caracteres proibidos no shortentry para _
	for(i = 0; i < len; i++) {
		char c = name[i];
		if(c == '.')
			dotPosition = i;

		for(int j = 0; j < prohibitedLen; j++) {
			if(c == prohibited[j]) {
				c = '_';
				break;
			}
		}

		name[i] = toupper(c);
	}

	// Copia todo o nome ate chegar no ultimo ponto ou na 8 posicao, qual vier primeiro
	for(i = 0; i < dotPosition && i < 8; i++) {
		output[i] = name[i];
	}

	// Copia os 3 primeiros caracteres da extensao na parte da extensao
	if(dotPosition) {
		for(i = dotPosition+1, j = 8; j < 11 && i < len; i++, j++) {
			output[j] = name[i];
		}
	}
}

// Entrada caminho para arquivo da particao atual, retorna primeiro cluster de arquivo
uint32_t write_file_from_outside_to_clusters(char* path, uint32_t* file_size, char* command_name) {

  FILE *f = fopen(path, "rb");
  if(f == NULL) {
    return FREE_CLUSTER;
  }


  fseek(f, 0, SEEK_END);
  *file_size = ftell(f);
  uint32_t cluster_size = bs.BPB_BytsPerSec * bs.BPB_SecPerClus;
  uint32_t file_cluster_count = ceil(*file_size/(double)cluster_size);
  fseek(f, 0, SEEK_SET);
  // Aloca o espaco necessario para o arquivo no disco
  uint32_t chain_start = allocate_clusters(file_cluster_count);
  if(chain_start == FREE_CLUSTER) {
	  printf("%s: Unable to alocate clusters, disk is full?\n", command_name);
    return FREE_CLUSTER;
  }

  uint8_t* buffer = (uint8_t*)malloc(cluster_size);

	uint32_t curr_cluster = chain_start;
  for(uint32_t i = 0; i < file_cluster_count; i++) {
    uint32_t read_size = (i+1) ==  file_cluster_count ? *file_size - i*cluster_size : cluster_size;

    fread(buffer, read_size, 1, f);

    uint64_t write_pos = get_cluster_offset(curr_cluster) * cluster_size;
		fseek(disk, write_pos, SEEK_SET);
		fwrite(buffer, read_size, 1, disk);

		curr_cluster = get_cluster_info(curr_cluster);
  }

  free(buffer);
  
  return chain_start;
}

void get_entry_name(char* entry_name) {
	int i;
	for(i = 4; i < strlen(entry_name); i++) {
		if(entry_name[i] == '/') {
			entry_name[0] = 0x00;
			return;
		}
		entry_name[i - 4] = entry_name[i];
	}
	entry_name[i - 4] = '\0';
}

// Função que move arquivo do path atual para um novo lugar
void mv(char* entry_path, char* entry_new_path) {
	// Flag se esta movendo de uma pasta da particao ou de dentro da imagem
	int in_image = 0; // 0 DISCO 1 IMAGEM
	if(!memcmp(entry_path, "img/", 4)) in_image = 1;

	// Flag se esta movendo para a particao ou para dentro da imagem
	int out_image = 0; // 0 DISCO 1 IMAGEM
	if(!memcmp(entry_new_path, "img/", 4)) out_image = 1;
	if(!in_image && !out_image) {
		printf("mv: you can't move a file from partion to partition, you can either move from partition to image or from image to partition!\n");
		return;
	}

	char currPath[11];
	memcpy(currPath, directory_stack->name, 11);

	printf("%d %d %s %s %s\n", in_image, out_image, entry_path, entry_new_path, currPath);

	if(!in_image && out_image) {
		get_entry_name(entry_new_path);
		if(entry_new_path[0] == 0) {
			printf("mv: '%s': File must be in current directory!\n", entry_new_path);
			return;
		}
		char formated_entry_path[11];
		create_formated_name(formated_entry_path, entry_new_path);

		if(formated_entry_path[0] == 0) {
			printf("mv: '%s': Invalid entry name, can't move folder!\n", entry_new_path);
			return;
		}

		uint32_t file_size;
		uint32_t file_cluster_chain_start = write_file_from_outside_to_clusters(entry_path, &file_size, "mv");
		if(file_cluster_chain_start == FREE_CLUSTER) return;

		touch(formated_entry_path);
		// Procura entrada com mesmo nome no diretório
		for(int entry_pos = 0; entry_pos < directory_stack->quantity; entry_pos++) {
			uint8_t status_byte = directory_stack->entries[entry_pos].short_dir.DIR_Name[0];
			// Se status_byte == E5 significa que o espaço está livre
			if(status_byte == 0xE5) continue;
			// Se for dir de long name ignora
			if((directory_stack->entries[entry_pos].short_dir.DIR_Attr & ATTR_LONG_NAME_MASK) == ATTR_LONG_NAME) continue;
			
			// Se não possui mais entradas no diretório, retorna com erro
			if(status_byte == 0x00) {
				printf("mv: '%s': No such file\n", entry_path);
				return;
			};

			// Se achou o arquivo e 
			if(!memcmp(formated_entry_path, directory_stack->entries[entry_pos].short_dir.DIR_Name, 11)) {
				uint32_t curr_cluster = (directory_stack->entries[entry_pos].short_dir.DIR_FstClusHI<<16) | directory_stack->entries[entry_pos].short_dir.DIR_FstClusLO;				
				write_in_fat(curr_cluster, &FREE_CLUSTER_POINTER);

				// Seta os atributos
				directory_stack->entries[entry_pos].short_dir.DIR_FstClusLO = file_cluster_chain_start & 0x0000FFFF;
				directory_stack->entries[entry_pos].short_dir.DIR_FstClusHI = (file_cluster_chain_start & 0xFFFF0000) >> 16;
				directory_stack->entries[entry_pos].short_dir.DIR_FileSize = file_size;

				// Copia para a memória
				fseek(disk, get_entry_disk_position(directory_stack->cluster, entry_pos), SEEK_SET);
				fwrite(&directory_stack->entries[entry_pos], sizeof(DirEntry), 1, disk);
				return;
			};
		}
	}
	if(in_image && !out_image) {
		get_entry_name(entry_path);
		if(entry_path[0] == 0) {
			printf("mv: '%s': File must be in current directory!\n", entry_path);
			return;
		}
		char formated_entry_path[11];
		create_formated_name(formated_entry_path, entry_path);

		DirEntry aux;
		uint32_t previous_position_disk;
		// Procura entrada com mesmo nome no diretório
		for(int entry_pos = 0; entry_pos < directory_stack->quantity; entry_pos++) {
			uint8_t status_byte = directory_stack->entries[entry_pos].short_dir.DIR_Name[0];
			// Se status_byte == E5 significa que o espaço está livre
			if(status_byte == 0xE5) continue;
			// Se for dir de long name ignora
			if((directory_stack->entries[entry_pos].short_dir.DIR_Attr & ATTR_LONG_NAME_MASK) == ATTR_LONG_NAME) continue;
			
			// Se não possui mais entradas no diretório, retorna com erro
			if(status_byte == 0x00) {
				printf("mv: '%s': No such file\n", entry_path);
				return;
			};

			// Se achou o arquivo e 
			if(!memcmp(formated_entry_path, directory_stack->entries[entry_pos].short_dir.DIR_Name, 11)) {
				// Se não existir flag de pasta retorna com erro
				if((directory_stack->entries[entry_pos].short_dir.DIR_Attr & ATTR_DIRECTORY) == ATTR_DIRECTORY) {
					printf("mv: '%s': Can't move a folder\n", entry_path);
					return;
				};
				
				// Copia para o auxiliar
				memcpy(&aux, &directory_stack->entries[entry_pos], sizeof(DirEntry));

				// Procura a posição da dir_entry para atualização na imagem
				previous_position_disk = get_entry_disk_position(directory_stack->cluster, entry_pos);
				break;
			};
		}

		FILE* outfile = fopen(entry_new_path, "wb+");

		uint32_t curr_cluster = aux.short_dir.DIR_FstClusHI << 16 | aux.short_dir.DIR_FstClusLO;

		uint32_t cluster_size = bs.BPB_BytsPerSec * bs.BPB_SecPerClus;
		uint32_t file_size = aux.short_dir.DIR_FileSize;
		uint32_t file_cluster_count = ceil(file_size/(double)cluster_size);

		uint8_t* buffer = (uint8_t*)malloc(cluster_size);

		for(uint32_t i = 0; i < file_cluster_count; i++) {
			uint32_t read_size = (i+1) ==  file_cluster_count ? file_size - i*cluster_size : cluster_size;

			fseek(disk, get_cluster_offset(curr_cluster) * cluster_size, SEEK_SET);
			fread(buffer, read_size, 1, disk);
			fwrite(buffer, read_size, 1, outfile);
			
			curr_cluster = get_cluster_info(curr_cluster);
  	}

		fseek(disk, previous_position_disk, SEEK_SET);
		fwrite(&AVAILABLE_ENTRY_POINTER, 1, 1, disk);
		fclose(outfile);

		read_dir();
	}
	if(in_image && out_image) {
		get_entry_name(entry_path);
		if(entry_path[0] == 0) {
			printf("mv: '%s': File must be in current directory!\n", entry_path);
			return;
		}
		char formated_entry_path[11];
		create_formated_name(formated_entry_path, entry_path);

		if(formated_entry_path[0] == 0) {
			printf("mv: '%s': Invalid entry name, can't move folder!\n", entry_path);
			return;
		}
		
		get_entry_name(entry_new_path);
		if(entry_path[0] == 0) {
			printf("mv: '%s': New directory must be in the current directory!\n", entry_path);
			return;
		}
		if(!strcmp(currPath, ".")) return;
		if(directory_stack_count == 0 && !strcmp(entry_new_path, "..")) return;

		DirEntry aux;
		uint32_t previous_position_disk;
		// Procura entrada com mesmo nome no diretório
		for(int entry_pos = 0; entry_pos < directory_stack->quantity; entry_pos++) {
			uint8_t status_byte = directory_stack->entries[entry_pos].short_dir.DIR_Name[0];
			// Se status_byte == E5 significa que o espaço está livre
			if(status_byte == 0xE5) continue;
			// Se for dir de long name ignora
			if((directory_stack->entries[entry_pos].short_dir.DIR_Attr & ATTR_LONG_NAME_MASK) == ATTR_LONG_NAME) continue;
			
			// Se não possui mais entradas no diretório, retorna com erro
			if(status_byte == 0x00) {
				printf("mv: '%s': No such file\n", entry_path);
				return;
			};

			// Se achou o arquivo e 
			if(!memcmp(formated_entry_path, directory_stack->entries[entry_pos].short_dir.DIR_Name, 11)) {
				// Se não existir flag de pasta retorna com erro
				if((directory_stack->entries[entry_pos].short_dir.DIR_Attr & ATTR_DIRECTORY) == ATTR_DIRECTORY) {
					printf("mv: '%s': Can't move a folder\n", entry_path);
					return;
				};
				
				// Copia para o auxiliar
				memcpy(&aux, &directory_stack->entries[entry_pos], sizeof(DirEntry));
				
				// Procura a posição da dir_entry para atualização na imagem
				previous_position_disk = get_entry_disk_position(directory_stack->cluster, entry_pos);
				break;
			};
		}


		if(!cd_wrapper(entry_new_path, "mv")) return;

		if(!touch_wrapper(entry_path, aux.short_dir.DIR_Attr, &aux)) return;

		if(!strcmp(entry_new_path, "..")) cd_wrapper(currPath, "mv");
		else cd_wrapper("..", "mv");

		fseek(disk, previous_position_disk, SEEK_SET);
		fwrite(&AVAILABLE_ENTRY_POINTER, 1, 1, disk);

		read_dir();
	}
}
*/

// Fecha o disco/imagem
void close_disk() {
	fclose(disk);
}