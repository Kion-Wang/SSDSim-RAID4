#ifndef __HASH_H__
#define __HASH_H__

#include "initialize.h"

void alloc_assert(void *p,char *s);



typedef struct buffer_info_Hash
{
	unsigned long read_hit;                      /*�����hit����ʾsector�����д�������û���еĴ���*/
	unsigned long read_miss_hit;  
	unsigned long write_hit;   
	unsigned long write_miss_hit;
	unsigned long write_free;   
	unsigned long eject;

	struct buffer_group *buffer_head;            /*as LRU head which is most recently used*/
	struct buffer_group *buffer_tail;            /*as LRU tail which is least recently used*/
	HASH_NODE	**nodeArray;     				 

	unsigned int max_buffer_sector;
	unsigned int buffer_sector_count;

	unsigned int	count;		                 /*AVL����Ľڵ�����*/
	int 			(*keyCompare)(HASH_NODE * , HASH_NODE *);
	int			(*free)(HASH_NODE *);
} tHash;


tHash *hash_create(int *freeFunc);
int hash_add(tHash *pHash ,  HASH_NODE *pInsertNode);
HASH_NODE *hash_find(tHash *pHash, HASH_NODE *pKeyNode);
int hash_del(tHash *pHash ,HASH_NODE *pDelNode);
void hash_node_free(tHash *pHash, HASH_NODE *pNode);
int hash_destroy(tHash *pHash);




















#endif