/*****************************************************************************************************************************
This project was supported by the National Basic Research 973 Program of China under Grant No.2011CB302301
Huazhong University of Science and Technology (HUST)   Wuhan National Laboratory for Optoelectronics

FileName�� pagemap.h
Author: Hu Yang		Version: 2.1	Date:2011/12/02
Description: 

History:
<contributor>     <time>        <version>       <desc>                   <e-mail>
Yang Hu	        2009/09/25	      1.0		    Creat SSDsim       yanghu@foxmail.com
                2010/05/01        2.x           Change 
Zhiming Zhu     2011/07/01        2.0           Change               812839842@qq.com
Shuangwu Zhang  2011/11/01        2.1           Change               820876427@qq.com
Chao Ren        2011/07/01        2.0           Change               529517386@qq.com
Hao Luo         2011/01/01        2.0           Change               luohao135680@gmail.com
*****************************************************************************************************************************/

#define _CRTDBG_MAP_ALLOC
 
#include "pagemap.h"
#include "ssd.h"
#include <limits.h>
#include "hash.h"

/************************************************
*����,�����ļ�ʧ��ʱ�������open �ļ��� error��
*************************************************/
void file_assert(int error,char *s)
{
	if(error == 0) return;
	printf("open %s error\n",s);
	getchar();
	exit(-1);
}

/*****************************************************
*����,�������ڴ�ռ�ʧ��ʱ�������malloc ������ error��
******************************************************/
void alloc_assert(void *p,char *s)//����
{
	if(p!=NULL) return;
	printf("malloc %s error\n",s);
	getchar();
	exit(-1);
}

/*********************************************************************************
*����
*A��������time_t��device��lsn��size��ope��<0ʱ�������trace error:.....��
*B��������time_t��device��lsn��size��ope��=0ʱ�������probable read a blank line��
**********************************************************************************/
void trace_assert(int64_t time_t,int device,unsigned int lsn,int size,int ope)//����
{
	if(time_t <0 || device < 0 || lsn < 0 || size < 0 || ope < 0)
	{
		printf("trace error:%lld %d %d %d %d\n",time_t,device,lsn,size,ope);
		getchar();
		exit(-1);
	}
	if(time_t == 0 && device == 0 && lsn == 0 && size == 0 && ope == 0)
	{
		printf("probable read a blank line\n");
		getchar();
	}
}


/************************************************************************************
*�����Ĺ����Ǹ�������ҳ��ppn���Ҹ�����ҳ���ڵ�channel��chip��die��plane��block��page
*�õ���channel��chip��die��plane��block��page���ڽṹlocation�в���Ϊ����ֵ
*************************************************************************************/
struct local *find_location(struct ssd_info *ssd,unsigned int ppn)
{
	struct local *location=NULL;
	unsigned int i=0;
	int pn,ppn_value=ppn;
	int page_plane=0,page_die=0,page_chip=0,page_channel=0;

	pn = ppn;

	
#ifdef DEBUG
	printf("enter find_location\n");
#endif

	location=(struct local *)malloc(sizeof(struct local));
    alloc_assert(location,"location");
	memset(location,0, sizeof(struct local));

	page_plane=ssd->parameter->page_block*ssd->parameter->block_plane; //number of page per plane
	page_die=page_plane*ssd->parameter->plane_die;                     //number of page per die
	page_chip=page_die*ssd->parameter->die_chip;                       //number of page per chip
	page_channel=page_chip*ssd->parameter->chip_channel[0];            //number of page per channel
	
	/*******************************************************************************
	*page_channel��һ��channel��page����Ŀ�� ppn/page_channel�͵õ������ĸ�channel��
	*��ͬ���İ취���Եõ�chip��die��plane��block��page
	********************************************************************************/
	location->channel = ppn/page_channel;
	location->chip = (ppn%page_channel)/page_chip;
	location->die = ((ppn%page_channel)%page_chip)/page_die;
	location->plane = (((ppn%page_channel)%page_chip)%page_die)/page_plane;
	location->block = ((((ppn%page_channel)%page_chip)%page_die)%page_plane)/ssd->parameter->page_block;
	location->page = (((((ppn%page_channel)%page_chip)%page_die)%page_plane)%ssd->parameter->page_block)%ssd->parameter->page_block;

	// if(pn == 1048577){
	// 	printf("1111 %u %d\n",location->channel, page_channel);
	// }
	

	return location;
}


/*****************************************************************************
*��������Ĺ����Ǹ��ݲ���channel��chip��die��plane��block��page���ҵ�������ҳ��
*�����ķ���ֵ�����������ҳ��
******************************************************************************/
unsigned int find_ppn(struct ssd_info * ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane,unsigned int block,unsigned int page)
{
	unsigned int ppn=0;
	unsigned int i=0;
	int page_plane=0,page_die=0,page_chip=0;
	int page_channel[100];                  /*��������ŵ���ÿ��channel��page��Ŀ*/

#ifdef DEBUG
	printf("enter find_psn,channel:%d, chip:%d, die:%d, plane:%d, block:%d, page:%d\n",channel,chip,die,plane,block,page);
#endif
    
	/*********************************************
	*�����plane��die��chip��channel�е�page����Ŀ
	**********************************************/
	page_plane=ssd->parameter->page_block*ssd->parameter->block_plane;
	page_die=page_plane*ssd->parameter->plane_die;
	page_chip=page_die*ssd->parameter->die_chip;
	while(i<ssd->parameter->channel_number)
	{
		page_channel[i]=ssd->parameter->chip_channel[i]*page_chip;
		i++;
	}

    /****************************************************************************
	*��������ҳ��ppn��ppn��channel��chip��die��plane��block��page��page�������ܺ�
	*****************************************************************************/
	i=0;
	while(i<channel)
	{
		ppn=ppn+page_channel[i];
		i++;
	}
	ppn=ppn+page_chip*chip+page_die*die+page_plane*plane+block*ssd->parameter->page_block+page;
	
	return ppn;
}

/********************************
*���������ǻ��һ�����������״̬
*********************************/
int set_entry_state(struct ssd_info *ssd, unsigned int lsn, unsigned int sectors)
{
    unsigned offset = lsn % ssd->parameter->subpage_page;
    unsigned available = ssd->parameter->subpage_page - offset;
    if (sectors > available) sectors = available;
    return sectors == 32 ? (int)UINT32_MAX : (int)(((1u << sectors) - 1u) << offset);
}

/**************************************************
*������Ԥ������������������������ҳ����û������ʱ��
*��ҪԤ��������ҳ����д���ݣ��Ա�֤�ܶ�������
***************************************************/
struct ssd_info *pre_process_page(struct ssd_info *ssd)
{
	int fl=0;
	unsigned int device,lsn,size,ope,lpn,full_page;
	unsigned int largest_lsn,sub_size,ppn,add_size=0;
	unsigned int i=0,j,k;
	int map_entry_new,map_entry_old,modify;
	int flag=0;
	char buffer_request[200];
	struct local *location;
	int64_t time;

	printf("\n");
	printf("begin pre_process_page.................\n");

	ssd->tracefile=fopen(ssd->tracefilename,"r");
	if(ssd->tracefile == NULL )      /*��trace�ļ����ж�ȡ����*/
	{
		printf("the trace file can't open\n");
		return NULL;
	}
	if(ssd->parameter->subpage_page == 32){
		full_page = 0xffffffff;
		}
	else{
		full_page=FULL_PAGE_STATE(ssd->parameter->subpage_page);
		}
	//full_page=FULL_PAGE_STATE(ssd->parameter->subpage_page);
	/*��������ssd������߼�������*/
	if(ssd->parameter->allocation_scheme==0 && ssd->parameter->dynamic_allocation == 2)
		largest_lsn=(unsigned int )(((ssd->parameter->chip_num - 1)*ssd->parameter->die_chip*ssd->parameter->plane_die*ssd->parameter->block_plane*ssd->parameter->page_block*ssd->parameter->subpage_page)*(1-ssd->parameter->overprovide));
	else 
		largest_lsn=(unsigned int )((ssd->parameter->chip_num*ssd->parameter->die_chip*ssd->parameter->plane_die*ssd->parameter->block_plane*ssd->parameter->page_block*ssd->parameter->subpage_page)*(1-ssd->parameter->overprovide));

	while(fgets(buffer_request,200,ssd->tracefile))
	{
		sscanf(buffer_request,"%lld %d %d %d %d",&time,&device,&lsn,&size,&ope);
		fl++;
		trace_assert(time,device,lsn,size,ope);                         /*���ԣ���������time��device��lsn��size��ope���Ϸ�ʱ�ͻᴦ��*/
		ssd->total_request_num++;
		add_size=0;                                                     /*add_size����������Ѿ�Ԥ�����Ĵ�С*/

		if(ope==1)                                                      /*����ֻ�Ƕ������Ԥ��������Ҫ��ǰ����Ӧλ�õ���Ϣ������Ӧ�޸�*/
		{
			while(add_size<size)
			{				
				lsn=lsn%largest_lsn;                                    /*��ֹ��õ�lsn������lsn����*/		
				sub_size=ssd->parameter->subpage_page-(lsn%ssd->parameter->subpage_page);		
				if(add_size+sub_size>=size)                             /*ֻ�е�һ������Ĵ�СС��һ��page�Ĵ�Сʱ�����Ǵ���һ����������һ��pageʱ������������*/
				{		
					sub_size=size-add_size;		
					add_size+=sub_size;		
				}

				if((sub_size>ssd->parameter->subpage_page)||(add_size>size))/*��Ԥ����һ���Ӵ�Сʱ�������С����һ��page�����Ѿ������Ĵ�С����size�ͱ���*/		
				{		
					printf("pre_process sub_size:%d\n",sub_size);		
				}

                /*******************************************************************************************************
				*�����߼�������lsn������߼�ҳ��lpn
				*�ж����dram��ӳ���map����lpnλ�õ�״̬
				*A�����״̬==0����ʾ��ǰû��д����������Ҫֱ�ӽ�ub_size��С����ҳд��ȥд��ȥ
				*B�����״̬>0����ʾ����ǰ��д��������Ҫ��һ���Ƚ�״̬����Ϊ��д��״̬��������ǰ��״̬���ص��������ĵط�
				********************************************************************************************************/
				lpn=lsn/ssd->parameter->subpage_page;
				if(ssd->dram->map->map_entry[lpn].state==0)                 /*״̬Ϊ0�����*/
				{
					/**************************************************************
					*�������get_ppn_for_pre_process�������ppn���ٵõ�location
					*�޸�ssd����ز�����dram��ӳ���map���Լ�location�µ�page��״̬
					***************************************************************/
					ppn=get_ppn_for_pre_process(ssd,lsn);                  
					location=find_location(ssd,ppn);
					ssd->program_count++;	
					ssd->channel_head[location->channel].program_count++;
					ssd->channel_head[location->channel].chip_head[location->chip].program_count++;		
					ssd->dram->map->map_entry[lpn].pn=ppn;	
					ssd->dram->map->map_entry[lpn].state=set_entry_state(ssd,lsn,sub_size);   //0001
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn=lpn;
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=ssd->dram->map->map_entry[lpn].state;
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=((~ssd->dram->map->map_entry[lpn].state)&full_page);
					
					free(location);
					location=NULL;
				}//if(ssd->dram->map->map_entry[lpn].state==0)
				else if(ssd->dram->map->map_entry[lpn].state>0)           /*״̬��Ϊ0�����*/
				{
					map_entry_new=set_entry_state(ssd,lsn,sub_size);      /*�õ��µ�״̬������ԭ����״̬���ĵ�һ��״̬*/
					map_entry_old=ssd->dram->map->map_entry[lpn].state;
                    modify=map_entry_new|map_entry_old;
					ppn=ssd->dram->map->map_entry[lpn].pn;
					location=find_location(ssd,ppn);

					ssd->program_count++;	
					ssd->channel_head[location->channel].program_count++;
					ssd->channel_head[location->channel].chip_head[location->chip].program_count++;		
					ssd->dram->map->map_entry[lsn/ssd->parameter->subpage_page].state=modify; 
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=modify;
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=((~modify)&full_page);
					
					free(location);
					location=NULL;
				}//else if(ssd->dram->map->map_entry[lpn].state>0)
				lsn=lsn+sub_size;                                         /*�¸����������ʼλ��*/
				add_size+=sub_size;                                       /*�Ѿ������˵�add_size��С�仯*/
			}//while(add_size<size)
		}//if(ope==1) 
	}	

	printf("\n");
	printf("pre_process is complete!\n");

	fclose(ssd->tracefile);

	for(i=0;i<ssd->parameter->channel_number;i++)
    for(j=0;j<ssd->parameter->die_chip;j++)
	for(k=0;k<ssd->parameter->plane_die;k++)
	{
		fprintf(ssd->outputfile,"chip:%d,die:%d,plane:%d have free page: %d\n",i,j,k,ssd->channel_head[i].chip_head[0].die_head[j].plane_head[k].free_page);				
		fflush(ssd->outputfile);
	}
	
	return ssd;
}

void invaild_page_pre(struct ssd_info *ssd, struct local *location){
	
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=0;             /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=0;              /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn=0;
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].raidID = -1;

	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;
	
}


void raid_pre_invalid(struct ssd_info *ssd){
	unsigned int ppn;
	struct local *location;

	ppn=get_ppn_for_pre_process(ssd, 0);                  
	location=find_location(ssd,ppn);
	ssd->program_count++;	
	ssd->channel_head[location->channel].program_count++;
	ssd->channel_head[location->channel].chip_head[location->chip].program_count++;		

	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn=0;
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=0;
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=0;

	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].raidID = ssd->stripe.nowStripe;
	
	
	invaild_page_pre(ssd, location);
	++ssd->allUsedPage;
	free(location);
	location=NULL;
}

void raid_pre_read(struct ssd_info *ssd, unsigned int lsn, unsigned int lpn, unsigned int sectors, unsigned int full_page)
{
    unsigned ppn = get_ppn_for_pre_process(ssd, lsn);
    struct local *loc = find_location(ssd, ppn);
    struct page_info *page = &ssd->channel_head[loc->channel].chip_head[loc->chip].die_head[loc->die].plane_head[loc->plane].blk_head[loc->block].page_head[loc->page];
    unsigned state = (unsigned)set_entry_state(ssd, lsn, sectors) & full_page;
    if (!state) { fprintf(stderr, "Invalid preprocessed page state\n"); exit(EXIT_FAILURE); }
    ssd->program_count++;
    ssd->channel_head[loc->channel].program_count++;
    ssd->channel_head[loc->channel].chip_head[loc->chip].program_count++;
    page->lpn = lpn; page->valid_state = state; page->free_state = full_page & ~state;
    page->raidID = ssd->stripe.nowStripe;
    if (lpn == ssd->stripe.checkLpn) {
        struct RaidInfo *stripe = &ssd->trip2Page[ssd->stripe.nowStripe];
        free(stripe->location);
        stripe->location = find_location(ssd, ppn);
        stripe->parity_state = state;
        stripe->Pchannel = loc->channel;
    } else {
        ssd->dram->map->map_entry[lpn].pn = ppn;
        ssd->dram->map->map_entry[lpn].state = state;
    }
    ++ssd->allUsedPage;
    free(loc);
}

void init_ppc_cache(struct ssd_info *ssd){
	int i = 0;
	unsigned int mask = FULL_PAGE_STATE(ssd->parameter->subpage_page);
	printf("start %llu\n", ssd->ssdToken);
	if(ssd->stripe.now != 0){
		ssd->trip2Page[ssd->stripe.nowStripe].Pchannel = 3 * ssd->perChanSSD + ssd->channelToken;
		for(i = 0; i < ssd->stripe.now; i++){
			if(ssd->ssdToken * ssd->perChanSSD + ssd->channelToken == ssd->trip2Page[ssd->stripe.nowStripe].Pchannel){
				if(++ssd->ssdToken >= (ssd->parameter->channel_number / ssd->perChanSSD)){
					ssd->ssdToken = 0;
					if(++ssd->channelToken >= ssd->perChanSSD){
						ssd->channelToken = 0;
						if(++ssd->chipToken >= ssd->parameter->chip_channel[0])
							ssd->chipToken = 0;
					}
				}
			}
			raid_pre_read(ssd, ssd->stripe.req[i].lpn * ssd->parameter->subpage_page, ssd->stripe.req[i].lpn, ssd->stripe.req[i].sub_size, ssd->stripe.req[i].full_page);

			ssd->trip2Page[ssd->stripe.nowStripe].lpn[i] = ssd->stripe.req[i].lpn;
			ssd->page2Trip[ssd->stripe.req[i].lpn] = ssd->stripe.nowStripe;
		}
		ppc_cache(ssd, ssd->stripe.nowStripe, mask, NULL, mask, ssd->stripe.req[i - 1].lpn, NULL);

		if(ssd->channelToken == 0){
			ssd->stripe.check = 3;
		}
	}
	printf("end %llu %u\n", ssd->ssdToken, ssd->stripe.now);
}

struct ssd_info *pre_process_page_raid_1(struct ssd_info *ssd)
{
	int fl=0;
	unsigned int device,lsn,size,ope,lpn,full_page;
	unsigned int largest_lsn,sub_size,ppn,add_size=0;
	unsigned int i=0,j,k;
	int map_entry_new,map_entry_old,modify;
	int flag=0;
	char buffer_request[200];
	struct local *location;
	int64_t time;
	
	unsigned int mask = FULL_PAGE_STATE(ssd->parameter->subpage_page);
	printf("\n");
	printf("begin pre_process_page.................\n");

	ssd->tracefile=fopen(ssd->tracefilename,"r");
	if(ssd->tracefile == NULL )      /*��trace�ļ����ж�ȡ����*/
	{
		printf("the trace file can't open\n");
		return NULL;
	}
	if(ssd->parameter->subpage_page == 32){
		full_page = 0xffffffff;
		}
	else{
		full_page=FULL_PAGE_STATE(ssd->parameter->subpage_page);
		}
	//full_page=FULL_PAGE_STATE(ssd->parameter->subpage_page);
	/*��������ssd������߼�������*/
	largest_lsn=(unsigned int )(((ssd->parameter->chip_num - 1)*ssd->parameter->die_chip*ssd->parameter->plane_die*ssd->parameter->block_plane*ssd->parameter->page_block*ssd->parameter->subpage_page)*(1-ssd->parameter->overprovide));
	largest_lsn = ssd->stripe.checkStart - 1;
	largest_lsn *= ssd->parameter->subpage_page;
	while(fgets(buffer_request,200,ssd->tracefile))
	{
		sscanf(buffer_request,"%lld %d %d %d %d",&time,&device,&lsn,&size,&ope);
		fl++;
		trace_assert(time,device,lsn,size,ope);                         /*���ԣ���������time��device��lsn��size��ope���Ϸ�ʱ�ͻᴦ��*/
		ssd->total_request_num++;
		add_size=0;                                                     /*add_size����������Ѿ�Ԥ�����Ĵ�С*/

		if(ope==1)                                                      /*����ֻ�Ƕ������Ԥ��������Ҫ��ǰ����Ӧλ�õ���Ϣ������Ӧ�޸�*/
		{
			while(add_size<size)
			{				
				lsn=lsn%largest_lsn;                                    /*��ֹ��õ�lsn������lsn����*/		
				sub_size=ssd->parameter->subpage_page-(lsn%ssd->parameter->subpage_page);		
				if(add_size+sub_size>=size)                             /*ֻ�е�һ������Ĵ�СС��һ��page�Ĵ�Сʱ�����Ǵ���һ����������һ��pageʱ������������*/
				{		
					sub_size=size-add_size;		
					add_size+=sub_size;		
				}

				if((sub_size>ssd->parameter->subpage_page)||(add_size>size))/*��Ԥ����һ���Ӵ�Сʱ�������С����һ��page�����Ѿ������Ĵ�С����size�ͱ���*/		
				{		
					printf("pre_process sub_size:%d\n",sub_size);		
				}

                /*******************************************************************************************************
				*�����߼�������lsn������߼�ҳ��lpn
				*�ж����dram��ӳ���map����lpnλ�õ�״̬
				*A�����״̬==0����ʾ��ǰû��д����������Ҫֱ�ӽ�ub_size��С����ҳд��ȥд��ȥ
				*B�����״̬>0����ʾ����ǰ��д��������Ҫ��һ���Ƚ�״̬����Ϊ��д��״̬��������ǰ��״̬���ص��������ĵط�
				********************************************************************************************************/
				lpn=lsn/ssd->parameter->subpage_page;
				for(i = 0; i < ssd->stripe.all; ++i){
					if(ssd->stripe.req[i].lpn == lpn && ssd->stripe.req[i].state != 0){
						break;
					}
				}
				if(i != ssd->stripe.all && ssd->stripe.req[i].lpn == lpn && ssd->stripe.req[i].state != 0){
					 ssd->stripe.req[i].state |= set_entry_state(ssd,lsn,sub_size);
				}else if(ssd->dram->map->map_entry[lpn].state==0)                 /*״̬Ϊ0�����*/
				{
					/**************************************************************
					*�������get_ppn_for_pre_process�������ppn���ٵõ�location
					*�޸�ssd����ز�����dram��ӳ���map���Լ�location�µ�page��״̬
					***************************************************************/
					ssd->stripe.req[ssd->stripe.now].lpn = lpn;
					ssd->stripe.req[ssd->stripe.now].state = set_entry_state(ssd,lsn,sub_size);				
					ssd->stripe.req[ssd->stripe.now].req = ssd->raidReq;
					ssd->stripe.req[ssd->stripe.now].sub_size = sub_size;
					ssd->stripe.req[ssd->stripe.now].full_page = full_page;
					
					if(++ssd->stripe.now == (ssd->stripe.all - 1)){
						unsigned int i, j;
						for(i = 0, j = 0; i < ssd->stripe.all; ++i){
							if(i == ssd->stripe.check){
								raid_pre_read(ssd, ssd->stripe.checkLpn * ssd->parameter->subpage_page, ssd->stripe.checkLpn, ssd->parameter->subpage_page, full_page);
								
								//ssd->trip2Page[ssd->stripe.nowStripe].lpn[i] = ssd->stripe.checkLpn;
								//ssd->trip2Page[ssd->stripe.nowStripe].check[i] = CHECK_RAID;
								
								//++ssd->stripe.checkLpn;
								continue;
							}
							if(ssd->stripe.req[j].req->operation != 0)
								printf("%d %d\n", j, ssd->stripe.req[j].req->operation);
							raid_pre_read(ssd, ssd->stripe.req[j].lpn * ssd->parameter->subpage_page, ssd->stripe.req[j].lpn, ssd->stripe.req[j].sub_size, ssd->stripe.req[j].full_page);
							
							ssd->trip2Page[ssd->stripe.nowStripe].lpn[j] = ssd->stripe.req[j].lpn;
							//ssd->trip2Page[ssd->stripe.nowStripe].check[j] = VAIL_DRAID;
							ssd->page2Trip[ssd->stripe.req[j].lpn] = ssd->stripe.nowStripe;
							
							
							ssd->stripe.req[j].state = 0;
							++j;						
						}
						
						ssd->trip2Page[ssd->stripe.nowStripe].allChange = 0;
						ssd->trip2Page[ssd->stripe.nowStripe].nowChange = 0;
						ssd->trip2Page[ssd->stripe.nowStripe].using = 1;
						ssd->stripe.nowStripe++;
						
						ssd->stripe.now = 0;
						if(ssd->channelToken == 0){
							ssd->stripe.check = 3;
						}
					}
				}//if(ssd->dram->map->map_entry[lpn].state==0)
				else if(ssd->dram->map->map_entry[lpn].state>0)           /*״̬��Ϊ0�����*/
				{
					map_entry_new=set_entry_state(ssd,lsn,sub_size);      /*�õ��µ�״̬������ԭ����״̬���ĵ�һ��״̬*/
					map_entry_old=ssd->dram->map->map_entry[lpn].state;
                    modify=map_entry_new|map_entry_old;
					ppn=ssd->dram->map->map_entry[lpn].pn;
					location=find_location(ssd,ppn);

					ssd->program_count++;	
					ssd->channel_head[location->channel].program_count++;
					ssd->channel_head[location->channel].chip_head[location->chip].program_count++;		
					ssd->dram->map->map_entry[lsn/ssd->parameter->subpage_page].state=modify; 
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=modify;
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=((~modify)&full_page);
					
					free(location);
					location=NULL;
				}//else if(ssd->dram->map->map_entry[lpn].state>0)
				lsn=lsn+sub_size;                                         /*�¸����������ʼλ��*/
				add_size+=sub_size;                                       /*�Ѿ������˵�add_size��С�仯*/
			}//while(add_size<size)
		}//if(ope==1) 
	}	

	printf("\n");
	printf("pre_process is complete!\n");

	fclose(ssd->tracefile);

	for(i=0;i<ssd->parameter->channel_number;i++)
    for(j=0;j<ssd->parameter->die_chip;j++)
	for(k=0;k<ssd->parameter->plane_die;k++)
	{
		fprintf(ssd->outputfile,"chip:%d,die:%d,plane:%d have free page: %d\n",i,j,k,ssd->channel_head[i].chip_head[0].die_head[j].plane_head[k].free_page);				
		fflush(ssd->outputfile);
	}
	
	return ssd;
}


struct ssd_info *pre_process_page_raid(struct ssd_info *ssd)
{
	int fl=0;
	unsigned int device,size,ope,lpn,full_page;
	unsigned int sub_size=ssd->parameter->subpage_page,ppn,add_size=0;
	unsigned int largest_lsn, lsn;
	unsigned int i=0,j,k;
	int map_entry_new,map_entry_old,modify;
	int flag=0;
	char buffer_request[200];
	struct local *location;
	int64_t time;
	
	unsigned int mask = FULL_PAGE_STATE(ssd->parameter->subpage_page);
	printf("\n");
	printf("begin pre_process_page.................\n");

	ssd->tracefile=fopen(ssd->tracefilename,"r");
	if(ssd->tracefile == NULL )      /*��trace�ļ����ж�ȡ����*/
	{
		printf("the trace file can't open\n");
		return NULL;
	}
	if(ssd->parameter->subpage_page == 32){
		full_page = 0xffffffff;
		}
	else{
		full_page=FULL_PAGE_STATE(ssd->parameter->subpage_page);
		}
	//full_page=FULL_PAGE_STATE(ssd->parameter->subpage_page);
	/*��������ssd������߼�������*/
	largest_lsn=(unsigned int )(((ssd->parameter->chip_num - 1)*ssd->parameter->die_chip*ssd->parameter->plane_die*ssd->parameter->block_plane*ssd->parameter->page_block*ssd->parameter->subpage_page)*(1-ssd->parameter->overprovide));
	largest_lsn = ssd->stripe.checkStart - 1;
	largest_lsn *= ssd->parameter->subpage_page;
	lsn=lsn%largest_lsn;
	printf("largest_lsn is %d\n", largest_lsn);
	while(fgets(buffer_request,200,ssd->tracefile))
	{
		sscanf(buffer_request,"%lld %d %d %d %d",&time,&device,&lsn,&size,&ope);
		fl++;
		trace_assert(time,device,lsn,size,ope);                         /*���ԣ���������time��device��lsn��size��ope���Ϸ�ʱ�ͻᴦ��*/
		ssd->total_request_num++;
		add_size=0;                                                     /*add_size����������Ѿ�Ԥ�����Ĵ�С*/
		lsn=lsn%largest_lsn;
		if(ope==1)                                                      /*����ֻ�Ƕ������Ԥ��������Ҫ��ǰ����Ӧλ�õ���Ϣ������Ӧ�޸�*/
		{
			lpn=lsn/ssd->parameter->subpage_page;
			unsigned long long last_lpn=(lsn+size-1)/ssd->parameter->subpage_page;
			unsigned long long first_lpn=lsn/ssd->parameter->subpage_page;
			while(lpn<=last_lpn)
			{				
				int nextLpn = lpn;
				lpn %= ssd->stripe.checkStart;
				// if(lpn == 4242673){
				// 	printf("create\n");
				// 	abort();
				// }
				lpn += 1;
				for(i = 0; i < ssd->stripe.all - 1; ++i){
					if(ssd->stripe.req[i].lpn == lpn && ssd->stripe.req[i].state != 0){
						break;
					}
				}
				if(i != ssd->stripe.all && ssd->stripe.req[i].lpn == lpn && ssd->stripe.req[i].state != 0){
					 ssd->stripe.req[i].state |= full_page;
				}else if(ssd->dram->map->map_entry[lpn].state==0)                 /*״̬Ϊ0�����*/
				{
					/**************************************************************
					*�������get_ppn_for_pre_process�������ppn���ٵõ�location
					*�޸�ssd����ز�����dram��ӳ���map���Լ�location�µ�page��״̬
					***************************************************************/
					ssd->stripe.req[ssd->stripe.now].lpn = lpn;
					ssd->stripe.req[ssd->stripe.now].state = full_page;				
					ssd->stripe.req[ssd->stripe.now].req = ssd->raidReq;
					ssd->stripe.req[ssd->stripe.now].sub_size = sub_size;
					ssd->stripe.req[ssd->stripe.now].full_page = full_page;
					
					if(++ssd->stripe.now == (ssd->stripe.all - 1)){
						unsigned int i, j;
						for(i = 0, j = 0; i < ssd->stripe.all; ++i){
							if(i == ssd->stripe.check){
								raid_pre_read(ssd, ssd->stripe.checkLpn * ssd->parameter->subpage_page, ssd->stripe.checkLpn, ssd->parameter->subpage_page, full_page);
								
								//ssd->trip2Page[ssd->stripe.nowStripe].lpn[i] = ssd->stripe.checkLpn;
								//ssd->trip2Page[ssd->stripe.nowStripe].check[i] = CHECK_RAID;
								
								//++ssd->stripe.checkLpn;
								continue;
							}
							if(ssd->stripe.req[j].req->operation != 0)
								printf("%d %d\n", j, ssd->stripe.req[j].req->operation);
							raid_pre_read(ssd, ssd->stripe.req[j].lpn * ssd->parameter->subpage_page, ssd->stripe.req[j].lpn, \
								ssd->stripe.req[j].sub_size, ssd->stripe.req[j].full_page);
							
							ssd->trip2Page[ssd->stripe.nowStripe].lpn[j] = ssd->stripe.req[j].lpn;
							//ssd->trip2Page[ssd->stripe.nowStripe].check[j] = VAIL_DRAID;
							ssd->page2Trip[ssd->stripe.req[j].lpn] = ssd->stripe.nowStripe;
							
							
							ssd->stripe.req[j].state = 0;
							++j;						
						}
						
						ssd->trip2Page[ssd->stripe.nowStripe].allChange = 0;
						ssd->trip2Page[ssd->stripe.nowStripe].nowChange = 0;
						ssd->trip2Page[ssd->stripe.nowStripe].using = 1;
						ssd->stripe.nowStripe++;
						
						ssd->stripe.now = 0;
						if(ssd->ssdToken == 0 && ssd->channelToken == 0){
							ssd->stripe.check = 3;
						}
					}
				}//if(ssd->dram->map->map_entry[lpn].state==0)
				else if(ssd->dram->map->map_entry[lpn].state>0)           /*״̬��Ϊ0�����*/
				{
					map_entry_new=full_page;      /*�õ��µ�״̬������ԭ����״̬���ĵ�һ��״̬*/
					map_entry_old=ssd->dram->map->map_entry[lpn].state;
                    modify=map_entry_new|map_entry_old;
					ppn=ssd->dram->map->map_entry[lpn].pn;
					location=find_location(ssd,ppn);

					ssd->program_count++;	
					ssd->channel_head[location->channel].program_count++;
					ssd->channel_head[location->channel].chip_head[location->chip].program_count++;		
					ssd->dram->map->map_entry[lpn].state=modify; 
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=modify;
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=((~modify)&full_page);
					
					free(location);
					location=NULL;
				}//else if(ssd->dram->map->map_entry[lpn].state>0)
				lpn = ++nextLpn;                                     /*�Ѿ������˵�add_size��С�仯*/
			}//while(add_size<size)
		}//if(ope==1) 
	}	

	printf("\n");
	printf("pre_process is complete!\n");

	fclose(ssd->tracefile);

	for(i=0;i<ssd->parameter->channel_number;i++)
    for(j=0;j<ssd->parameter->die_chip;j++)
	for(k=0;k<ssd->parameter->plane_die;k++)
	{
		fprintf(ssd->outputfile,"chip:%d,die:%d,plane:%d have free page: %d\n",i,j,k,ssd->channel_head[i].chip_head[0].die_head[j].plane_head[k].free_page);				
		fflush(ssd->outputfile);
	}
	
	return ssd;
}


/**************************************
*����������ΪԤ����������ȡ����ҳ��ppn
*��ȡҳ�ŷ�Ϊ��̬��ȡ�;�̬��ȡ
**************************************/
unsigned int get_ppn_for_pre_process(struct ssd_info *ssd,unsigned int lsn)     
{
	unsigned int channel=0,chip=0,die=0,plane=0; 
	unsigned int ppn,lpn;
	unsigned int active_block;
	unsigned int channel_num=0,chip_num=0,die_num=0,plane_num=0;

#ifdef DEBUG
	printf("enter get_psn_for_pre_process\n");
#endif

	channel_num=ssd->parameter->channel_number;
	chip_num=ssd->parameter->chip_channel[0];
	die_num=ssd->parameter->die_chip;
	plane_num=ssd->parameter->plane_die;
	lpn=lsn/ssd->parameter->subpage_page;

	if (ssd->parameter->allocation_scheme==0)                           /*��̬��ʽ�»�ȡppn*/
	{
		if (ssd->parameter->dynamic_allocation==0)                      /*��ʾȫ��̬��ʽ�£�Ҳ����channel��chip��die��plane��block�ȶ��Ƕ�̬����*/
		{
			channel=ssd->token;
			ssd->token=(ssd->token+1)%ssd->parameter->channel_number;
			chip=ssd->channel_head[channel].token;
			ssd->channel_head[channel].token=(chip+1)%ssd->parameter->chip_channel[0];
			die=ssd->channel_head[channel].chip_head[chip].token;
			ssd->channel_head[channel].chip_head[chip].token=(die+1)%ssd->parameter->die_chip;
			plane=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
			ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane+1)%ssd->parameter->plane_die;
		} 
		else if (ssd->parameter->dynamic_allocation==1)                 /*��ʾ�붯̬��ʽ��channel��̬������package��die��plane��̬����*/                 
		{
			channel=lpn%ssd->parameter->channel_number;
			chip=ssd->channel_head[channel].token;
			ssd->channel_head[channel].token=(chip+1)%ssd->parameter->chip_channel[0];
			die=ssd->channel_head[channel].chip_head[chip].token;
			ssd->channel_head[channel].chip_head[chip].token=(die+1)%ssd->parameter->die_chip;
			plane=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
			ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane+1)%ssd->parameter->plane_die;
		}else if(ssd->parameter->dynamic_allocation == 2){
			channel = ssd->ssdToken * ssd->perChanSSD + ssd->channelToken;
			chip = ssd->chipToken; 

			die=ssd->channel_head[channel].chip_head[chip].token;
			ssd->channel_head[channel].chip_head[chip].token=(die+1)%ssd->parameter->die_chip;
			plane=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
			ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane+1)%ssd->parameter->plane_die;

			if(++ssd->ssdToken >= (ssd->parameter->channel_number / ssd->perChanSSD)){
				ssd->ssdToken = 0;
				if(++ssd->channelToken >= ssd->perChanSSD){
					ssd->channelToken = 0;
					if(++ssd->chipToken >= ssd->parameter->chip_channel[0]){
						ssd->chipToken = 0;
						// abort();
					}
				}
			}
			
		}
	} 
	else if (ssd->parameter->allocation_scheme==1)                       /*��ʾ��̬���䣬ͬʱҲ��0,1,2,3,4,5��6�в�ͬ��̬���䷽ʽ*/
	{
		switch (ssd->parameter->static_allocation)
		{
			
			case 0:         
			{
				channel=(lpn/(plane_num*die_num*chip_num))%channel_num;
				chip=lpn%chip_num;
				die=(lpn/chip_num)%die_num;
				plane=(lpn/(die_num*chip_num))%plane_num;
				break;
			}
		case 1:
			{
				channel=lpn%channel_num;
				chip=(lpn/channel_num)%chip_num;
				die=(lpn/(chip_num*channel_num))%die_num;
				plane=(lpn/(die_num*chip_num*channel_num))%plane_num;
							
				break;
			}
		case 2:
			{
				channel=lpn%channel_num;
				chip=(lpn/(plane_num*channel_num))%chip_num;
				die=(lpn/(plane_num*chip_num*channel_num))%die_num;
				plane=(lpn/channel_num)%plane_num;
				break;
			}
		case 3:
			{
				channel=lpn%channel_num;
				chip=(lpn/(die_num*channel_num))%chip_num;
				die=(lpn/channel_num)%die_num;
				plane=(lpn/(die_num*chip_num*channel_num))%plane_num;
				break;
			}
		case 4:  
			{
				channel=lpn%channel_num;
				chip=(lpn/(plane_num*die_num*channel_num))%chip_num;
				die=(lpn/(plane_num*channel_num))%die_num;
				plane=(lpn/channel_num)%plane_num;
							
				break;
			}
		case 5:   
			{
				channel=lpn%channel_num;
				chip=(lpn/(plane_num*die_num*channel_num))%chip_num;
				die=(lpn/channel_num)%die_num;
				plane=(lpn/(die_num*channel_num))%plane_num;
							
				break;
			}
		default : return 0;
		}
	}
    
	/******************************************************************************
	*�����������䷽���ҵ�channel��chip��die��plane��������������ҵ�active_block
	*���Ż��ppn
	******************************************************************************/
	// printf("chann %d  chip %d die %d plane %d\n", channel, chip, die, plane);
	if(find_active_block(ssd,channel,chip,die,plane)==FAILURE)
	{
		// printf(" %u %u\n", ssd->allUsedPage, ssd->allPage);
		printf("the read operation is expand the capacity of SSD\n");	
		return 0;
	}
	active_block=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
	if(write_page(ssd,channel,chip,die,plane,active_block,&ppn)==ERROR)
	{
		return 0;
	}

	return ppn;
}

int check_plane(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane){
	unsigned int free_page_num;
	unsigned int cumulate_free_page_num=0;
	unsigned int cumulate_free_lsb_num=0;
	unsigned int i;
	struct plane_info candidate_plane;
	candidate_plane = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane];
	for(i=0;i<ssd->parameter->block_plane;i++){
		if(candidate_plane.blk_head[i].free_lsb_num+candidate_plane.blk_head[i].free_csb_num+candidate_plane.blk_head[i].free_msb_num!=candidate_plane.blk_head[i].free_page_num){
			printf("Meta data Wrong, BLOCK LEVEL!!\n");
			return FALSE;
			}
		cumulate_free_page_num+=candidate_plane.blk_head[i].free_page_num;
		cumulate_free_lsb_num+=candidate_plane.blk_head[i].free_lsb_num;
		}
	if(cumulate_free_page_num!=candidate_plane.free_page){
		printf("Meta data Wrong, PLANE LEVEL!!\n");
		if(cumulate_free_page_num>candidate_plane.free_page){
			printf("The data in plane_info is SMALLER than the blocks.\n");
			}
		else{
			printf("The data in plane_info is GREATER than the blocks.\n");
			}
		return FALSE;
		}
	return TRUE;
}

void invaild_semi_page(struct ssd_info *ssd, struct local *location){
	
	unsigned long long raidid = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].raidID;
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].raidID = -1;
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].strip_page--;

	if(ssd->trip2Page[raidid].location && ssd->trip2Page[raidid].allChange == 1){
		ssd->trip2Page[raidid].allChange = 0;
		ssd->channel_head[ssd->trip2Page[raidid].location->channel].chip_head[ssd->trip2Page[raidid].location->chip].die_head[ssd->trip2Page[raidid].location->die].plane_head[ssd->trip2Page[raidid].location->plane].blk_head[ssd->trip2Page[raidid].location->block].strip_page--;
	}
}

void creat_semi_page(struct ssd_info *ssd, struct local *location, unsigned long long raidid){
	if(ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state != 0){
		abort();
	}
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].raidID = raidid;
}

void invaild_page(struct ssd_info *ssd, struct local *location){
	if(ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state != 0){
		unsigned long long lpn = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn;

		unsigned long long raidid = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].raidID;
		struct buffer_group *buffer_node=NULL, key;
		key.group = raidid;
		buffer_node = (struct buffer_group*)hash_find(ssd->dram->buffer, (HASH_NODE *)&key);
		if(0 && lpn != ssd->stripe.checkLpn && ssd->dram->map->map_entry[lpn].oldppa == -1 && buffer_node != NULL){
			int i = 0;
			ssd->dram->map->map_entry[lpn].oldppa = find_ppn(ssd, location->channel, location->chip, location->die,location->plane, location->block, location->page);
			if(location->channel == 2 && location->chip == 2 && location->die == 0 && location->plane == 0 &&location->block == 6){
				if(location->page == 2){
					printf("invaild_page2, %llu,\n", lpn);
				}else if(location->page == 14){
					printf("invaild_page6 %llu,\n", lpn);
					// abort();
				}
			}
			for(i = 0; i < ssd->stripe.all - 1; ++i){
				int nowlpn = ssd->trip2Page[raidid].lpn[i];
				if(ssd->dram->map->map_entry[nowlpn].state != 0 && ssd->dram->map->map_entry[nowlpn].oldppa == -1){
					break;
				}
			}
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].strip_page++;
			if(ssd->trip2Page[raidid].location && ssd->trip2Page[raidid].allChange == 0){
				ssd->trip2Page[raidid].allChange = 1;
				ssd->channel_head[ssd->trip2Page[raidid].location->channel].chip_head[ssd->trip2Page[raidid].location->chip].die_head[ssd->trip2Page[raidid].location->die].plane_head[ssd->trip2Page[raidid].location->plane].blk_head[ssd->trip2Page[raidid].location->block].strip_page++;
			}
			if(i == ssd->stripe.all - 1){	
				// if(raidid == 9154)
				// 	printf("eject,2,,%lld\n", raidid);
				for(i = 0; i < ssd->stripe.all - 1; ++i){
					int nowlpn = ssd->trip2Page[raidid].lpn[i];
					if(ssd->dram->map->map_entry[nowlpn].state != 0 && ssd->dram->map->map_entry[nowlpn].oldppa != -1){
						struct local *loc = find_location(ssd, ssd->dram->map->map_entry[nowlpn].oldppa);
						ssd->dram->map->map_entry[nowlpn].oldppa = -1;
						invaild_semi_page(ssd, loc);
						free(loc);
					}
				}
				if(ssd->trip2Page[raidid].location){
					invaild_page(ssd, ssd->trip2Page[raidid].location);
					free(ssd->trip2Page[raidid].location);
					ssd->trip2Page[raidid].location = NULL;
				}
					
			}
		}else{
			if(location->channel == 2 && location->chip == 2 && location->die == 0 && location->plane == 0 &&location->block == 6){
				if(location->page == 6){
					printf("invaild_page62 %lld lpn %u %u %llu\n", ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].raidID, ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn, ssd->stripe.checkLpn, lpn);
					// abort();
					/* No ordinary data-map lookup for parity pages. */
					
				}
			}
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].raidID = -1;
		}

		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=0;             /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=0;              /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
		// ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn=0;
		
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;
	}else{
		printf("%d\n", ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn);
		printf("%lld\n", ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].raidID);
		abort();
	}

}

void judge_erase_node(struct ssd_info *ssd, struct local *location){
	return;
	struct direct_erase *direct_erase_node,*new_direct_erase;
	if (ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num==ssd->parameter->page_block)    
	{
		int i = 0;
		int count = 0;
		for(i=0;i<ssd->parameter->page_block;i++){
			if(ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[i].valid_state!=0){
				++count;	
			}
			if(ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[i].raidID != -1){
				return;
			}
		}
		if(count){
			printf("create error %d\n",count);
			abort();
		}
		
		new_direct_erase=(struct direct_erase *)malloc(sizeof(struct direct_erase));
        alloc_assert(new_direct_erase,"new_direct_erase");
		memset(new_direct_erase,0, sizeof(struct direct_erase));

		new_direct_erase->block=location->block;
		new_direct_erase->next_node=NULL;
		direct_erase_node=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node;
		if (direct_erase_node==NULL)
		{
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node=new_direct_erase;
		} 
		else
		{
			new_direct_erase->next_node=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node;
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node=new_direct_erase;
		}
	}
}


/***************************************************************************************************
*������������������channel��chip��die��plane�����ҵ�һ��active_blockȻ���������block�����ҵ�һ��ҳ��
*������find_ppn�ҵ�ppn��
****************************************************************************************************/
struct ssd_info *get_ppn(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane,struct sub_request *sub)
{

	int old_ppn=-1;
	unsigned int ppn,lpn,full_page;
	unsigned int active_block;
	unsigned int block;
	unsigned int page,flag=0,flag1=0;
	unsigned int old_state=0,state=0,copy_subpage=0;
	struct local *location;
	struct direct_erase *direct_erase_node,*new_direct_erase;
	struct gc_operation *gc_node;
	struct gc_operation *gc_node_tmp;
	
	unsigned int lsn;
	unsigned int temp_channel, temp_chip, temp_die,temp_plane, temp_block;

	unsigned int i=0,j=0,k=0,l=0,m=0,n=0;
	float debug = 0;

	unsigned int mask=FULL_PAGE_STATE(ssd->parameter->subpage_page);

#ifdef DEBUG
	printf("enter get_ppn,channel:%d, chip:%d, die:%d, plane:%d\n",channel,chip,die,plane);
#endif
	if(ssd->parameter->subpage_page == 32){
		full_page = 0xffffffff;
		}
	else{
		full_page=FULL_PAGE_STATE(ssd->parameter->subpage_page);
		}
	lpn=sub->lpn;
	--ssd->chanSubWLenNow[channel];
	

	i = 0;
	j = 0;
	/*************************************************************************************
	*���ú���find_active_block��channel��chip��die��plane�ҵ���Ծblock
	*�����޸����channel��chip��die��plane��active_block�µ�last_write_page��free_page_num
	**************************************************************************************/
	//ssd->chipWrite[channel * ssd->parameter->chip_channel[0] + chip]++;
	if(find_active_block(ssd,channel,chip,die,plane)==FAILURE)                      
	{
		printf("ERROR :there is no free page in channel:%d, chip:%d, die:%d, plane:%d\n",channel,chip,die,plane);	
		return ssd;
	}

	active_block=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page++;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--;
	/*add by winks*/
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].write_frequently = sub->write_freqently;
	/*end add by winks*/
	if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page>=ssd->parameter->page_block)
	{
		printf("error! the last write page larger than the number of pages per block!!\n");
		while(1){}
	}

	block=active_block;	
	page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page;	
	if(page%3==0){
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_lsb=page;
		ssd->free_lsb_count--;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_lsb_num--;
        ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_lsb_num--;
		//ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--;
		ssd->write_lsb_count++;
		ssd->newest_write_lsb_count++;
		//**********************
		sub->allocated_page_type = TARGET_LSB;
		//**********************
		}
	else if(page%3==2){
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_msb=page;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_msb_num--;
		//ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--;
		ssd->write_msb_count++;
		ssd->free_msb_count--;
		ssd->newest_write_msb_count++;
		//**********************
		sub->allocated_page_type = TARGET_MSB;
		//**********************
		}
	else{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_csb=page;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_csb_num--;
		ssd->write_csb_count++;
		ssd->free_csb_count--;
		ssd->newest_write_csb_count++;
		sub->allocated_page_type = TARGET_CSB;
		}
	if(lpn == ssd->stripe.checkLpn){
		if(ssd->trip2Page[sub->raidNUM].location){
			//printf("entry");
			//while(1);
			if(ssd->channel_head[ssd->trip2Page[sub->raidNUM].location->channel].chip_head[ssd->trip2Page[sub->raidNUM].location->chip].die_head[ssd->trip2Page[sub->raidNUM].location->die].plane_head[ssd->trip2Page[sub->raidNUM].location->plane].blk_head[ssd->trip2Page[sub->raidNUM].location->block].page_head[ssd->trip2Page[sub->raidNUM].location->page].raidID != sub->raidNUM){
				printf("don`t accord %lld %d\n",ssd->channel_head[ssd->trip2Page[sub->raidNUM].location->channel].chip_head[ssd->trip2Page[sub->raidNUM].location->chip].die_head[ssd->trip2Page[sub->raidNUM].location->die].plane_head[ssd->trip2Page[sub->raidNUM].location->plane].blk_head[ssd->trip2Page[sub->raidNUM].location->block].page_head[ssd->trip2Page[sub->raidNUM].location->page].raidID , sub->raidNUM);
				abort();
			}
			invaild_page(ssd, ssd->trip2Page[sub->raidNUM].location);
			judge_erase_node(ssd, ssd->trip2Page[sub->raidNUM].location);
			free(ssd->trip2Page[sub->raidNUM].location);
			ssd->trip2Page[sub->raidNUM].location = NULL;
		}
		sub->ppn=find_ppn(ssd,channel,chip,die,plane,block,page);
        ssd->trip2Page[sub->raidNUM].parity_state = sub->state;
	}else if(ssd->dram->map->map_entry[lpn].state==0)                                       /*this is the first logical page*/
	{

		if(ssd->dram->map->map_entry[lpn].pn!=0)
		{
			printf("Error in get_ppn()\n");
		}
		ssd->dram->map->map_entry[lpn].pn=find_ppn(ssd,channel,chip,die,plane,block,page);
		ssd->dram->map->map_entry[lpn].state=sub->state;
	}
	else                                                                            /*����߼�ҳ�����˸��£���Ҫ��ԭ����ҳ��ΪʧЧ*/
	{
		
		ppn=ssd->dram->map->map_entry[lpn].pn;
		location=find_location(ssd,ppn);
		if(	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn!=lpn)
		{
			printf("%d, %d\n", lpn, ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn);
			printf("\nError in get_ppn()\n");
		}
		
		//ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=0;             /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
		//ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=0;              /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
		//ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn=0;
		//ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;

		invaild_page(ssd, location);
		/*******************************************************************************************
		*��block��ȫ��invalid��ҳ������ֱ��ɾ�������ڴ���һ���ɲ����Ľڵ㣬����location�µ�plane����
		********************************************************************************************/
		/*if (ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num==ssd->parameter->page_block)    
		{
			new_direct_erase=(struct direct_erase *)malloc(sizeof(struct direct_erase));
            alloc_assert(new_direct_erase,"new_direct_erase");
			memset(new_direct_erase,0, sizeof(struct direct_erase));

			new_direct_erase->block=location->block;
			new_direct_erase->next_node=NULL;
			direct_erase_node=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node;
			if (direct_erase_node==NULL)
			{
				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node=new_direct_erase;
			} 
			else
			{
				new_direct_erase->next_node=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node;
				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node=new_direct_erase;
			}
		}*/
		judge_erase_node(ssd, location);

		free(location);
		location=NULL;
		ssd->dram->map->map_entry[lpn].pn=find_ppn(ssd,channel,chip,die,plane,block,page);
		ssd->dram->map->map_entry[lpn].state=(ssd->dram->map->map_entry[lpn].state|sub->state);
	}

	
	if (lpn != ssd->stripe.checkLpn) sub->ppn = ssd->dram->map->map_entry[lpn].pn;                                      /*�޸�sub�������ppn��location�ȱ���*/

	
	
	sub->location->channel=channel;
	sub->location->chip=chip;
	sub->location->die=die;
	sub->location->plane=plane;
	sub->location->block=active_block;
	sub->location->page=page;

	ssd->program_count++;                                                           /*�޸�ssd��program_count,free_page�ȱ���*/
	ssd->channel_head[channel].program_count++;
	ssd->channel_head[channel].chip_head[chip].program_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].lpn=lpn;	
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].valid_state=sub->state;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].free_state=((~(sub->state))&full_page);
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].written_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].raidID = sub->raidNUM;
	ssd->write_flash_count++;

	if(sub->lpn == ssd->stripe.checkLpn){
		//printf("%d,%d\n", sub->lpn, ssd->stripe.checkLpn);
		ssd->trip2Page[sub->raidNUM].location = find_location(ssd, sub->ppn);
	}
	if (ssd->parameter->active_write==0)                                            /*���û���������ԣ�ֻ����gc_hard_threshold�������޷��ж�GC����*/
	{                                                                               /*���plane�е�free_page����Ŀ����gc_hard_threshold���趨����ֵ�Ͳ���gc����*/
		if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page<(ssd->parameter->page_block*ssd->parameter->block_plane*(ssd->parameter->gc_hard_threshold + 0.00)))
		{
			static int gcCount = 0;
			static int64_t pre = -1;
			struct gc_operation *preNode = ssd->channel_head[channel].gc_command;
			/*while(preNode){
				if(preNode->chip == chip && preNode->plane == plane)
					break;
				preNode = preNode->next_node;
			}
			if(preNode)
				return ssd;*/
			//int64_t timeDiff = ssd->current_time - ssd->preRequestArriveTime[ssd->preRequestArriveTimeIndex];
			//fprintf(ssd->gcIntervalFile,"%lld\n", timeDiff);
			
			pre = ssd->current_time;
			
			struct gc_operation * last_node = ssd->channel_head[channel].last_gc_command;
			fprintf(ssd->gcCreateRequest,"%d\n", ssd->request_queue_length);	
			gc_node=(struct gc_operation *)malloc(sizeof(struct gc_operation));
			alloc_assert(gc_node,"gc_node");
			memset(gc_node,0, sizeof(struct gc_operation));

			gc_node->next_node=NULL;
			gc_node->chip=chip;
			gc_node->die=die;
			gc_node->plane=plane;
			gc_node->block=0xffffffff;
			gc_node->page=0;
			gc_node->state=GC_WAIT;
			gc_node->priority=GC_UNINTERRUPT;
			gc_node->wl_flag=0;
			/*add by winks*/
			if(last_node != NULL)
				last_node->next_node = gc_node;
			else 
				ssd->channel_head[channel].gc_command=gc_node;
			ssd->channel_head[channel].last_gc_command = gc_node;
			/*end add by winks*/
			ssd->gc_request++;
		}
		/*add by winks*/
		/*lpn++;
		if(ssd->dram->map->map_entry[lpn].state) {
			unsigned long ppn_for_gc = ssd->dram->map->map_entry[lpn].pn;
			location = find_location(ssd,ppn_for_gc);
			offsetTmp = location->channel * ((ssd->parameter->plane_die)*(ssd->parameter->die_chip)*(ssd->parameter->chip_channel[0])) + \
				        	location->chip * ((ssd->parameter->plane_die)*(ssd->parameter->die_chip)) + \
				        	location->die * (ssd->parameter->plane_die) + location->plane; 
			posTmp = offsetTmp % 32;
			offsetTmp = offsetTmp / 32;
			if((*(ssd->gc_map_plane + offsetTmp)) & (1U << posTmp)) {

				if(ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node == NULL) {
					struct gc_operation* pre_for_gc, *gc_node_for_gc;
					gc_node_for_gc = ssd->channel_head[location->channel].gc_command;
					if((gc_node_for_gc->chip == location->chip) && (gc_node_for_gc->die == location->die) && (gc_node_for_gc->plane == location->plane)) {
						pre_for_gc = NULL;
					} else {
						while(gc_node_for_gc->next_node) {
							if((gc_node_for_gc->next_node->chip == location->chip) && (gc_node_for_gc->next_node->die == location->die) && (gc_node_for_gc->next_node->plane == location->plane)) {
								pre_for_gc = gc_node_for_gc;
								gc_node_for_gc = gc_node_for_gc->next_node;
								break;
							}
							pre_for_gc = gc_node_for_gc;
							gc_node_for_gc = gc_node_for_gc->next_node;
						}
					}

					if((gc_node_for_gc != ssd->channel_head[location->channel].last_gc_command)){
						if(pre_for_gc == NULL) {
							ssd->channel_head[location->channel].gc_command = gc_node_for_gc->next_node;
							gc_node_for_gc->next_node = ssd->channel_head[location->channel].gc_command->next_node;
							ssd->channel_head[location->channel].gc_command->next_node = gc_node_for_gc;
							if(ssd->channel_head[location->channel].last_gc_command == ssd->channel_head[location->channel].gc_command)
								ssd->channel_head[location->channel].last_gc_command = gc_node_for_gc;
						}else {
							pre_for_gc->next_node = gc_node_for_gc->next_node;
							gc_node_for_gc->next_node = pre_for_gc->next_node->next_node;
							pre_for_gc->next_node->next_node = gc_node_for_gc;
							if(ssd->channel_head[location->channel].last_gc_command == pre_for_gc->next_node)
								ssd->channel_head[location->channel].last_gc_command = gc_node_for_gc;
						}
						
						//printf("change!!!!\n");
					}
					//printf("change!!!\n");
				}
				
			}
			free(location);
		}
		lpn--;*/
		/*end add by winks*/
		
		/***Reset BET****/
		/*if(ssd->table_count >= ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num){
				ssd->table_count = 0;
				ssd->total_erase_count = 0;
				ssd->bet_num = 0;
				for(i = 0 ; i<ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num; i++){
						ssd->BET[i] = 0;
				}
			}*/
		
		//debug = ssd->total_erase_count/ssd->table_count;
		//printf("debug3::wl_value:%f, total_erase_count:%f, table_count:%f, wl_page_move:%d\n", debug, ssd->total_erase_count, ssd->table_count, ssd->wl_page_move_count);
		/***Generate WL request through gc operation when threshold is over the present value***/
		/* if((ssd->table_count != 0) && (ssd->total_erase_count/ssd->table_count >= WL_THRESHOLD) && ((ssd->wl_request+ssd->table_count) < (ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num)))
		{
			while(ssd->BET[ssd->bet_num]==1){
				ssd->bet_num= (ssd->bet_num+1)%(ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num);
			}

				gc_node=(struct gc_operation *)malloc(sizeof(struct gc_operation));
				alloc_assert(gc_node,"gc_node");
				memset(gc_node,0, sizeof(struct gc_operation));

				gc_node->next_node=NULL;
				gc_node->chip=(ssd->bet_num%(ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num))/(ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip);
				gc_node->die=(ssd->bet_num%(ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num)%(ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip))/(ssd->parameter->block_plane*ssd->parameter->plane_die);
				gc_node->plane=(ssd->bet_num%(ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num)%(ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip)%(ssd->parameter->block_plane*ssd->parameter->plane_die))/(ssd->parameter->block_plane);
				gc_node->block=(ssd->bet_num%(ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num)%(ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip)%(ssd->parameter->block_plane*ssd->parameter->plane_die)%ssd->parameter->block_plane);
				gc_node->page=0;
				gc_node->state=GC_WAIT;
				gc_node->priority=GC_UNINTERRUPT;        //test
				gc_node->wl_flag = 1;
				temp_channel = ssd->bet_num/(ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num);
				gc_node->next_node=ssd->channel_head[temp_channel].gc_command;
				ssd->channel_head[temp_channel].gc_command=gc_node;
				ssd->wl_request++;
				ssd->bet_num= (ssd->bet_num+1)%(ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num);
				
		} */
	} 
/* 	if(check_plane(ssd, channel, chip, die, plane) == FALSE){
		printf("Something Wrong Happened.\n");
		return FAILURE;
		} */
	
	return ssd;
}



 unsigned int get_ppn_for_gc(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane)     
{
	unsigned int ppn;
	unsigned int active_block,block,page;

#ifdef DEBUG
	printf("enter get_ppn_for_gc,channel:%d, chip:%d, die:%d, plane:%d\n",channel,chip,die,plane);
#endif

	if(find_active_block(ssd,channel,chip,die,plane)!=SUCCESS)
	{
		printf("\n\n Error int get_ppn_for_gc().\n");
		return 0xffffffff;
	}
	
	/*if(find_active_block_for_gc(ssd,channel,chip,die,plane, 0, 0)!= SUCCESS)                      
	{
		printf("ERROR :there is no free page in channel:%d, chip:%d, die:%d, plane:%d\n",channel,chip,die,plane);	
		return 0xffffffff;
	}*/
    
	active_block=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
	
	if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page>ssd->parameter->page_block)
	{
		
		if(find_active_block(ssd,channel,chip,die,plane) != SUCCESS)
		{
			printf("\n\n Error int get_ppn_for_gc().\n");
			return 0xffffffff;
		}
		active_block=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
		
	}

	//*********************************************
	/*
	if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_msb<ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_csb){
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_msb+3;
		}
	else if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_csb<ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_lsb){
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_csb+3;
		}
	else{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_lsb+3;
		}
		*/
	//*********************************************

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page++;	
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--;

	if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page>ssd->parameter->page_block)
	{
		printf("DDD error! the last write page larger than 64!!\n");
		while(1){}
		
	}

	block=active_block;	
	page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page;	

	if(page%3==0){
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_lsb=page;
		ssd->free_lsb_count--;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_lsb_num--;
        ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_lsb_num--;
		//ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--;
		ssd->write_lsb_count++;
		ssd->newest_write_lsb_count++;
		}
	else if(page%3==2){
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_msb=page;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_msb_num--;
		//ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--;
		ssd->write_msb_count++;
		ssd->free_msb_count--;
		ssd->newest_write_msb_count++;
		}
	else{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_csb=page;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_csb_num--;
		ssd->write_csb_count++;
		ssd->free_csb_count--;
		ssd->newest_write_csb_count++;
		}

	ppn=find_ppn(ssd,channel,chip,die,plane,block,page);

	ssd->program_count++;
	ssd->channel_head[channel].program_count++;
	ssd->channel_head[channel].chip_head[chip].program_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].written_count++;
	ssd->write_flash_count++;

	/*add by winks*/
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].write_frequently = 0;
	/*end add by winks*/
	return ppn;

}

/*********************************************************************************************************************
* ��־�� ��2011��7��28���޸�   
*�����Ĺ��ܾ���erase_operation������������channel��chip��die��plane�µ�block������
*Ҳ���ǳ�ʼ�����block����ز�����eg��free_page_num=page_block��invalid_page_num=0��last_write_page=-1��erase_count++
*�������block�����ÿ��page����ز���ҲҪ�޸ġ�
*********************************************************************************************************************/

Status erase_operation(struct ssd_info * ssd,unsigned int channel ,unsigned int chip ,unsigned int die ,unsigned int plane ,unsigned int block)
{
	unsigned int flag, i;
	flag = 0;
	int bet_temp;
	for(i=0;i<ssd->parameter->page_block;i++){
		if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state!=0){
			flag = 1;
			}
		}
	
	if(flag==1){
		printf("Erasing a block with valid data: %d, %d, %d, %d, %d.\n",channel, chip, die, plane, block);
		return FAILURE;
		}
	fprintf(ssd->raidOutfile,"%d\n", ssd->request_queue_length);
	ssd->chipGc[channel * ssd->parameter->chip_channel[0] + chip]++;
	unsigned int origin_free_page_num, origin_free_lsb_num, origin_free_csb_num, origin_free_msb_num;
	origin_free_page_num = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num;
	origin_free_lsb_num = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_lsb_num;
	origin_free_csb_num = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_csb_num;
	origin_free_msb_num = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_msb_num;
	/* if(origin_free_lsb_num+origin_free_csb_num+origin_free_msb_num!=origin_free_page_num){
		printf("WRONG METADATA in BLOCK LEVEL.(erase_operation)\n");
		} */
	//printf("ERASE_OPERATION: %d, %d, %d, %d, %d.\n",channel,chip,die,plane,block);
	//unsigned int i=0;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num=ssd->parameter->page_block;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num=0;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page=-1;
	//*****************************************************
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_lsb=-3;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_csb=-2;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_msb=-1;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_lsb_num=(int)((ssd->parameter->page_block+2)/3);
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_csb_num=(int)((ssd->parameter->page_block+1)/3);
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_msb_num=(int)(ssd->parameter->page_block/3);
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_lsb_num=0;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].fast_erase=FALSE;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].dr_state=DR_STATE_NULL;
	//*****************************************************
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].erase_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].strip_page = 0;
	for (i=0;i<ssd->parameter->page_block;i++)
	{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].free_state=PG_SUB;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state=0;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].lpn=-1;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].raidID = -1;
	}
	ssd->erase_count++;
	ssd->channel_head[channel].erase_count++;			
	ssd->channel_head[channel].chip_head[chip].erase_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page+=ssd->parameter->page_block;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page-=origin_free_page_num;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_lsb_num+=((ssd->parameter->page_block+2)/3);
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_lsb_num-=origin_free_lsb_num;
	ssd->free_lsb_count+=((ssd->parameter->page_block+2)/3);
	ssd->free_csb_count+=((ssd->parameter->page_block+1)/3);
	ssd->free_msb_count+=(ssd->parameter->page_block/3);
	ssd->free_lsb_count-=origin_free_lsb_num;
	ssd->free_csb_count-=origin_free_csb_num;
	ssd->free_msb_count-=origin_free_msb_num;
	
	
	
	ssd->total_erase_count++;
	
	
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].read_count = 0;
	/* 
	if(ssd->UBT[bet_temp] == 1){
				ssd->UBT[bet_temp] = 0;
				ssd->rr_erase_count++;
				ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].read_count = 0;
				printf("debug_rr: rr_erase_count:%d, erase_count:%d\n", ssd->rr_erase_count, ssd->erase_count);
				//printf("debug::wl_value:%d, total_erase_count:%d, table_count:%d\n", ssd->total_erase_count/ssd->table_count, ssd->total_erase_count, ssd->table_count);
	} */

	return SUCCESS;

}


/**************************************************************************************
*��������Ĺ����Ǵ���INTERLEAVE_TWO_PLANE��INTERLEAVE��TWO_PLANE��NORMAL�µĲ����Ĳ�����
***************************************************************************************/
Status erase_planes(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die1, unsigned int plane1,unsigned int command)
{
	unsigned int die=0;
	unsigned int plane=0;
	unsigned int block=0;
	struct direct_erase *direct_erase_node=NULL;
	unsigned int block0=0xffffffff;
	unsigned int block1=0;

	if((ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node==NULL)||               
		((command!=INTERLEAVE_TWO_PLANE)&&(command!=INTERLEAVE)&&(command!=TWO_PLANE)&&(command!=NORMAL)))     /*���û�в�������������command���ԣ����ش���*/           
	{
		return ERROR;
	}

	/************************************************************************************************************
	*������������ʱ������Ҫ���Ͳ����������channel��chip���ڴ��������״̬����CHANNEL_TRANSFER��CHIP_ERASE_BUSY
	*��һ״̬��CHANNEL_IDLE��CHIP_IDLE��
	*************************************************************************************************************/
	block1=ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node->block;
	
	ssd->channel_head[channel].current_state=CHANNEL_GC;										
	ssd->channel_head[channel].current_time=ssd->current_time;										
	ssd->channel_head[channel].next_state=CHANNEL_IDLE;	

	ssd->channel_head[channel].chip_head[chip].current_state=CHIP_ERASE_BUSY;										
	ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
	ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;

	
	
	if(command==INTERLEAVE_TWO_PLANE)                                       /*�߼�����INTERLEAVE_TWO_PLANE�Ĵ���*/
	{
		for(die=0;die<ssd->parameter->die_chip;die++)
		{
			block0=0xffffffff;
			if(die==die1)
			{
				block0=block1;
			}
			for (plane=0;plane<ssd->parameter->plane_die;plane++)
			{
				direct_erase_node=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
				if(direct_erase_node!=NULL)
				{
					
					block=direct_erase_node->block; 
					
					if(block0==0xffffffff)
					{
						block0=block;
					}
					else
					{
						if(block!=block0)
						{
							continue;
						}

					}
					ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node=direct_erase_node->next_node;
					erase_operation(ssd,channel,chip,die,plane,block);     /*��ʵ�Ĳ��������Ĵ���*/
					free(direct_erase_node);                               
					direct_erase_node=NULL;
					ssd->direct_erase_count++;
				}

			}
		}

		ssd->interleave_mplane_erase_count++;                             /*������һ��interleave two plane erase����,���������������ʱ�䣬�Լ���һ��״̬��ʱ��*/
		ssd->channel_head[channel].next_state_predict_time=ssd->current_time+18*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tWB;       
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time-9*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tBERS;

	}
	else if(command==INTERLEAVE)                                          /*�߼�����INTERLEAVE�Ĵ���*/
	{
		for(die=0;die<ssd->parameter->die_chip;die++)
		{
			for (plane=0;plane<ssd->parameter->plane_die;plane++)
			{
				if(die==die1)
				{
					plane=plane1;
				}
				direct_erase_node=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
				if(direct_erase_node!=NULL)
				{
					block=direct_erase_node->block;
					ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node=direct_erase_node->next_node;
					erase_operation(ssd,channel,chip,die,plane,block);
					free(direct_erase_node);
					direct_erase_node=NULL;
					ssd->direct_erase_count++;
					break;
				}	
			}
		}
		ssd->interleave_erase_count++;
		ssd->channel_head[channel].next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC;       
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tBERS;
	}
	else if(command==TWO_PLANE)                                          /*�߼�����TWO_PLANE�Ĵ���*/
	{

		for(plane=0;plane<ssd->parameter->plane_die;plane++)
		{
			direct_erase_node=ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane].erase_node;
			if((direct_erase_node!=NULL))
			{
				block=direct_erase_node->block;
				if(block==block1)
				{
					ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane].erase_node=direct_erase_node->next_node;
					erase_operation(ssd,channel,chip,die1,plane,block);
					free(direct_erase_node);
					direct_erase_node=NULL;
					ssd->direct_erase_count++;
				}
			}
		}

		ssd->mplane_erase_conut++;
		ssd->channel_head[channel].next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC;      
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tBERS;
	}
	else if(command==NORMAL)                                             /*��ͨ����NORMAL�Ĵ���*/
	{
		
		direct_erase_node=ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node;
		block=direct_erase_node->block;
		/*add by winks*/
		if(ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].blk_head[block].write_frequently) {
			//printf("add  dir %ld\n", ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].blk_head[block].changed_count);
			ssd->invaild_page_of_change +=  ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].blk_head[block].changed_count;
			ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].blk_head[block].changed_count = 0;
		}
		/*end add by winks*/
		ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node=direct_erase_node->next_node;
		free(direct_erase_node);
		direct_erase_node=NULL;
		erase_operation(ssd,channel,chip,die1,plane1,block);

		ssd->direct_erase_count++;
		ssd->channel_head[channel].next_state_predict_time=ssd->current_time+5*ssd->parameter->time_characteristics.tWC;       								
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tWB+ssd->parameter->time_characteristics.tBERS;	

		if(ssd->gcIntervalFile)
			fprintf(ssd->gcIntervalFile,"%lld\t%lld\t%lld\n", ssd->current_time, ssd->channel_head[channel].chip_head[chip].next_state_predict_time, ssd->current_time - ssd->preRequestArriveTime[ssd->preRequestArriveTimeIndex]);
	}
	else
	{
		return ERROR;
	}

	direct_erase_node=ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node;

	if(((direct_erase_node)!=NULL)&&(direct_erase_node->block==block1))
	{
		return FAILURE; 
	}
	else
	{
		return SUCCESS;
	}
}

Status fast_erase_planes(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die1, unsigned int plane1,unsigned int command)
{
	unsigned int die=0;
	unsigned int plane=0;
	unsigned int block=0;
	struct direct_erase *direct_erase_node=NULL;
	unsigned int block0=0xffffffff;
	unsigned int block1=-1;
	/*
	if((ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].fast_erase_node==NULL)|| (command!=NORMAL))
	{
		return ERROR;
	}
	*/
	/************************************************************************************************************
	*������������ʱ������Ҫ���Ͳ����������channel��chip���ڴ��������״̬����CHANNEL_TRANSFER��CHIP_ERASE_BUSY
	*��һ״̬��CHANNEL_IDLE��CHIP_IDLE��
	*************************************************************************************************************/
	struct plane_info candidate_plane;
	candidate_plane = ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1];
	while(block < ssd->parameter->block_plane)
		{
		if((candidate_plane.blk_head[block].free_msb_num==ssd->parameter->page_block/2)&&(candidate_plane.blk_head[block].invalid_lsb_num==ssd->parameter->page_block/2)&&(candidate_plane.blk_head[block].fast_erase!=TRUE))
			{
			block1 = block;
			break;
			}
		block++;
		}
	//block1=ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].fast_erase_node->block;
	if(block1==-1){
		return FAILURE;
		}
	ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
	ssd->channel_head[channel].current_time=ssd->current_time;										
	ssd->channel_head[channel].next_state=CHANNEL_IDLE;	

	ssd->channel_head[channel].chip_head[chip].current_state=CHIP_ERASE_BUSY;										
	ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
	ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;

	
	if(command==NORMAL)                                             /*��ͨ����NORMAL�Ĵ���*/
	{
		//direct_erase_node=ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].fast_erase_node;
		//block=direct_erase_node->block;
		//ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].fast_erase_node=direct_erase_node->next_node;
		//free(direct_erase_node);
		//direct_erase_node=NULL;
		candidate_plane.blk_head[block1].fast_erase = TRUE;
		erase_operation(ssd,channel,chip,die1,plane1,block1);

		ssd->fast_gc_count++;
		ssd->channel_head[channel].next_state_predict_time=ssd->current_time+5*ssd->parameter->time_characteristics.tWC;       								
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tWB+ssd->parameter->time_characteristics.tBERS;
		return SUCCESS;
	}
	else
	{
		printf("Wrong Commend for fast gc.\n");
		return ERROR;
	}
	/*
	direct_erase_node=ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].fast_erase_node;

	if(((direct_erase_node)!=NULL)&&(direct_erase_node->block==block1))
	{
		return FAILURE; 
	}
	else
	{
		return SUCCESS;
	}
	*/
}


/*******************************************************************************************************************
*GC������ĳ��plane��free��������ֵ���д�������ĳ��plane������ʱ��GC����ռ�����plane���ڵ�die����Ϊdie��һ��������Ԫ��
*��һ��die��GC���������������ĸ�planeͬʱerase������interleave erase������GC����Ӧ������������ʱֹͣ���ƶ����ݺͲ���
*ʱ���У����Ǽ�϶ʱ�����ֹͣGC���������Է����µ�������󣬵��������������������϶ʱ�䣬����GC������������������
*GC��ֵ��һ������ֵ��һ��Ӳ��ֵ������ֵ��ʾ�������ֵ�󣬿��Կ�ʼ������GC���������ü�Ъʱ�䣬GC���Ա��µ��������жϣ�
*������Ӳ��ֵ��ǿ����ִ��GC�������Ҵ�GC�������ܱ��жϣ�ֱ���ص�Ӳ��ֵ���ϡ�
*������������棬�ҳ����die���е�plane�У���û�п���ֱ��ɾ����block��Ҫ���еĻ�������interleave two plane���ɾ��
*��Щblock�������ж���plane������ֱ��ɾ����block��ͬʱɾ�������еĻ��������ǵ������plane����ɾ��������Ҳ������Ļ���
*ֱ����������gc_parallelism�������н�һ��GC�������ú���Ѱ��ȫ��Ϊinvalid�Ŀ飬ֱ��ɾ�����ҵ���ֱ��ɾ���ķ���1��û����
*������-1��
*********************************************************************************************************************/
int gc_direct_erase(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane)     
{
	unsigned int lv_die=0,lv_plane=0;                                                           /*Ϊ����������ʹ�õľֲ����� Local variables*/
	unsigned int interleaver_flag=FALSE,muilt_plane_flag=FALSE;
	unsigned int normal_erase_flag=TRUE;

	struct direct_erase * direct_erase_node1=NULL;
	struct direct_erase * direct_erase_node2=NULL;

	direct_erase_node1=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
	if (direct_erase_node1==NULL)
	{
		return FAILURE;
	}
    
	/********************************************************************************************************
	*���ܴ���TWOPLANE�߼�����ʱ��������Ӧ��channel��chip��die��������ͬ��plane�ҵ�����ִ��TWOPLANE������block
	*����muilt_plane_flagΪTRUE
	*********************************************************************************************************/
	if((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE)
	{	
		for(lv_plane=0;lv_plane<ssd->parameter->plane_die;lv_plane++)
		{
			direct_erase_node2=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
			if((lv_plane!=plane)&&(direct_erase_node2!=NULL))
			{
				if((direct_erase_node1->block)==(direct_erase_node2->block))
				{
					muilt_plane_flag=TRUE;
					break;
				}
			}
		}
	}

	/***************************************************************************************
	*���ܴ���INTERLEAVE�߼�����ʱ��������Ӧ��channel��chip�ҵ�����ִ��INTERLEAVE������block
	*����interleaver_flagΪTRUE
	****************************************************************************************/
	if((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE)
	{
		for(lv_die=0;lv_die<ssd->parameter->die_chip;lv_die++)
		{
			if(lv_die!=die)
			{
				for(lv_plane=0;lv_plane<ssd->parameter->plane_die;lv_plane++)
				{
					if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node!=NULL)
					{
						interleaver_flag=TRUE;
						break;
					}
				}
			}
			if(interleaver_flag==TRUE)
			{
				break;
			}
		}
	}
    
	/************************************************************************************************************************
	*A������ȿ���ִ��twoplane������block���п���ִ��interleaver������block����ô��ִ��INTERLEAVE_TWO_PLANE�ĸ߼������������
	*B�����ֻ����ִ��interleaver������block����ô��ִ��INTERLEAVE�߼�����Ĳ�������
	*C�����ֻ����ִ��TWO_PLANE������block����ô��ִ��TWO_PLANE�߼�����Ĳ�������
	*D��û��������Щ�������ô��ֻ�ܹ�ִ����ͨ�Ĳ���������
	*************************************************************************************************************************/
	if ((muilt_plane_flag==TRUE)&&(interleaver_flag==TRUE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE)&&((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE))     
	{
		if(erase_planes(ssd,channel,chip,die,plane,INTERLEAVE_TWO_PLANE)==SUCCESS)
		{
			return SUCCESS;
		}
	} 
	else if ((interleaver_flag==TRUE)&&((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE))
	{
		if(erase_planes(ssd,channel,chip,die,plane,INTERLEAVE)==SUCCESS)
		{
			return SUCCESS;
		}
	}
	else if ((muilt_plane_flag==TRUE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE))
	{
		if(erase_planes(ssd,channel,chip,die,plane,TWO_PLANE)==SUCCESS)
		{
			return SUCCESS;
		}
	}

	if (normal_erase_flag==TRUE)                              /*����ÿ��plane���п���ֱ��ɾ����block��ֻ�Ե�ǰplane������ͨ��erase����������ֻ��ִ����ͨ����*/
	{
		if (erase_planes(ssd,channel,chip,die,plane,NORMAL)==SUCCESS)
		{
			return SUCCESS;
		} 
		else
		{
			return FAILURE;                                     /*Ŀ���planeû�п���ֱ��ɾ����block����ҪѰ��Ŀ����������ʵʩ��������*/
		}
	}
	return SUCCESS;
}

int gc_direct_fast_erase(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane)     
{
	unsigned int lv_die=0,lv_plane=0;                                                           /*Ϊ����������ʹ�õľֲ����� Local variables*/
	unsigned int interleaver_flag=FALSE,muilt_plane_flag=FALSE;
	unsigned int normal_erase_flag=TRUE;

	struct direct_erase * direct_erase_node1=NULL;
	struct direct_erase * direct_erase_node2=NULL;

	/*
	direct_erase_node1=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].fast_erase_node;
	if (direct_erase_node1==NULL)
	{
		return FAILURE;
	}
    */
	/********************************************************************************************************
	*���ܴ���TWOPLANE�߼�����ʱ��������Ӧ��channel��chip��die��������ͬ��plane�ҵ�����ִ��TWOPLANE������block
	*����muilt_plane_flagΪTRUE
	*********************************************************************************************************/
	if((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE)
	{	
		printf("Advanced commands are NOT supported.\n");
		return FAILURE;
	}

	/***************************************************************************************
	*���ܴ���INTERLEAVE�߼�����ʱ��������Ӧ��channel��chip�ҵ�����ִ��INTERLEAVE������block
	*����interleaver_flagΪTRUE
	****************************************************************************************/
	if((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE)
	{
		printf("Advanced commands are NOT supported.\n");
		return FAILURE;
	}
    
	/************************************************************************************************************************
	*A������ȿ���ִ��twoplane������block���п���ִ��interleaver������block����ô��ִ��INTERLEAVE_TWO_PLANE�ĸ߼������������
	*B�����ֻ����ִ��interleaver������block����ô��ִ��INTERLEAVE�߼�����Ĳ�������
	*C�����ֻ����ִ��TWO_PLANE������block����ô��ִ��TWO_PLANE�߼�����Ĳ�������
	*D��û��������Щ�������ô��ֻ�ܹ�ִ����ͨ�Ĳ���������
	*************************************************************************************************************************/
	/*
	if ((muilt_plane_flag==TRUE)&&(interleaver_flag==TRUE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE)&&((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE))     
	{
		if(erase_planes(ssd,channel,chip,die,plane,INTERLEAVE_TWO_PLANE)==SUCCESS)
		{
			return SUCCESS;
		}
	} 
	else if ((interleaver_flag==TRUE)&&((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE))
	{
		if(erase_planes(ssd,channel,chip,die,plane,INTERLEAVE)==SUCCESS)
		{
			return SUCCESS;
		}
	}
	else if ((muilt_plane_flag==TRUE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE))
	{
		if(erase_planes(ssd,channel,chip,die,plane,TWO_PLANE)==SUCCESS)
		{
			return SUCCESS;
		}
	}
	*/

	if (normal_erase_flag==TRUE)                              /*����ÿ��plane���п���ֱ��ɾ����block��ֻ�Ե�ǰplane������ͨ��erase����������ֻ��ִ����ͨ����*/
	{
		if (fast_erase_planes(ssd,channel,chip,die,plane,NORMAL)==SUCCESS)
		{
			return SUCCESS;
		} 
		else
		{
			return FAILURE;                                     /*Ŀ���planeû�п���ֱ��ɾ����block����ҪѰ��Ŀ����������ʵʩ��������*/
		}
	}
	else{
		printf("Wrong command for fast gc.\n");
		return FAILURE;
		}
	//return SUCCESS;
}


Status move_page(struct ssd_info * ssd, struct local *location, unsigned int * transfer_size)
{
	struct local *new_location=NULL;
	unsigned int free_state=0,valid_state=0;
	unsigned int lpn=0,old_ppn=0,ppn=0;
	//printf("state of this block: %d\n",ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].fast_erase);

	lpn=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn;
	valid_state=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state;
	free_state=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state;
	old_ppn=find_ppn(ssd,location->channel,location->chip,location->die,location->plane,location->block,location->page);      /*��¼�����Ч�ƶ�ҳ��ppn���Ա�map���߶���ӳ���ϵ�е�ppn������ɾ�������Ӳ���*/
	ppn=get_ppn_for_gc(ssd,location->channel,location->chip,location->die,location->plane);                /*�ҳ�����ppnһ�����ڷ���gc������plane��,����ʹ��copyback������Ϊgc������ȡppn*/

	new_location=find_location(ssd,ppn);                                                                   /*�����»�õ�ppn��ȡnew_location*/
	//printf("MOVE PAGE, lpn:%d, old_ppn:%d, new_ppn:%d, FROM %d,%d,%d,%d,%d,%d TO %d,%d,%d,%d,%d,%d.\n",lpn,old_ppn,ppn,location->channel,location->chip,location->die,location->plane,location->block,location->page,new_location->channel,new_location->chip,new_location->die,new_location->plane,new_location->block,new_location->page);
	/*
	if(new_location->channel==location->channel && new_location->chip==location->chip && new_location->die==location->die && new_location->plane==location->plane && new_location->block==location->block){
		printf("MOVE PAGE WRONG!!! Page is moved to the same block.\n");
		return FAILURE;
		}
	*/
	if(new_location->block == location->block){
		ssd->same_block_flag = 1;
		ppn=get_ppn_for_gc(ssd,location->channel,location->chip,location->die,location->plane);                /*�ҳ�����ppnһ�����ڷ���gc������plane��,����ʹ��copyback������Ϊgc������ȡppn*/
		new_location=find_location(ssd,ppn);  
		ssd->same_block_flag = 0;
	}
	if(new_location->block == location->block){
		printf("Data is moved to the same block!!!!!!\n");
		return FAILURE;
	}
	if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)
	{
		printf("Wrong COMMANDS.\n");
		return FAILURE;
		if (ssd->parameter->greed_CB_ad==1)                                                                /*̰����ʹ�ø߼�����*/
		{
			ssd->copy_back_count++;
			ssd->gc_copy_back++;
			while (old_ppn%2!=ppn%2)
			{
				ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].free_state=0;
				ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].lpn=0;
				ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].valid_state=0;
				ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].invalid_page_num++;
				
				free(new_location);
				new_location=NULL;

				ppn=get_ppn_for_gc(ssd,location->channel,location->chip,location->die,location->plane);    /*�ҳ�����ppnһ�����ڷ���gc������plane�У�����������ż��ַ����,����ʹ��copyback����*/
				ssd->program_count--;
				ssd->write_flash_count--;
				ssd->waste_page_count++;
			}
			if(new_location==NULL)
			{
				new_location=find_location(ssd,ppn);
			}
			
			ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].free_state=free_state;
			ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].lpn=lpn;
			ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].valid_state=valid_state;
		} 
		else
		{
			if (old_ppn%2!=ppn%2)
			{
				(*transfer_size) += size(valid_state);
			}
			else
			{

				ssd->copy_back_count++;
				ssd->gc_copy_back++;
			}
		}	
	} 
	else
	{
		(* transfer_size)+=size(valid_state);
	}
	ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].free_state=free_state;
	ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].lpn=lpn;
	ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].valid_state=valid_state;
	int raidID = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].raidID;
	ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].raidID = raidID;

	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=0;
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn=0;
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=0;
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;
	if((location->page)%3==0){
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_lsb_num++;
		}

	if (lpn != ssd->stripe.checkLpn && old_ppn==ssd->dram->map->map_entry[lpn].pn)                                                     /*�޸�ӳ���*/
	{
		ssd->dram->map->map_entry[lpn].pn=ppn;
	}
	if(lpn == ssd->stripe.checkLpn){
		ssd->trip2Page[raidID].location->channel = new_location->channel;
		ssd->trip2Page[raidID].location->chip = new_location->chip;
		ssd->trip2Page[raidID].location->die = new_location->die;
		ssd->trip2Page[raidID].location->plane = new_location->plane;
		ssd->trip2Page[raidID].location->block = new_location->block;
		ssd->trip2Page[raidID].location->page = new_location->page;
		++ssd->pageMoveRaid;
	}
	
	ssd->moved_page_count++;
	
	free(new_location);
	new_location=NULL;

	return SUCCESS;
}

/*******************************************************************************************************************************************
*Ŀ���planeû�п���ֱ��ɾ����block����ҪѰ��Ŀ����������ʵʩ�������������ڲ����жϵ�gc�����У��ɹ�ɾ��һ���飬����1��û��ɾ��һ���鷵��-1
*����������У����ÿ���Ŀ��channel,die�Ƿ��ǿ��е�,����invalid_page_num����block
********************************************************************************************************************************************/
int uninterrupt_gc(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane,struct gc_operation *gc_node)       
{
	unsigned int i=0;
	unsigned int block,active_block,transfer_size,free_page,page_move_count=0;                           /*��¼ʧЧҳ���Ŀ��*/
	struct local *  location=NULL;
	unsigned int total_invalid_page_num=0;
	unsigned char flag = 0;
	int gc_eject_count = INT_MAX;
	int invalid_page = INT_MIN;

	if(find_active_block(ssd,channel,chip,die,plane)!=SUCCESS)                                           /*��ȡ��Ծ��*/
	{
		printf("\n\n Error in uninterrupt_gc().\n");
		return ERROR;
	}
	active_block=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;

	invalid_page=INT_MIN;
	transfer_size=0;
	block=-1;
	if(gc_node->wl_flag == 2){
		block = gc_node->block;
	}else if(gc_node->wl_flag == 1){
		block = gc_node->block;
		ssd->wl_erase_count++;
	}else{
		// (this normal erase)
		ssd->normal_erase++;
		int iter = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blockIter++;
		iter %= ssd->parameter->block_plane;
		for(i=0;i<ssd->parameter->block_plane;i++)                                                           /*�������invalid_page�Ŀ�ţ��Լ�����invalid_page_num*/
		{	
			int prei = i;
			// i = (i + iter) % ssd->parameter->block_plane;
			total_invalid_page_num+=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;
			if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].free_page_num > 0){
				i = prei;
				continue;
				}
			// if((active_block!=i)&&(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num>invalid_page))						
			// {				
			// 	invalid_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;
			// 	block=i;						
			// }
			// if((active_block!=i)&&((ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num > invalid_page) || (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num >= (invalid_page) && (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].strip_page + 1) < gc_eject_count)))						
			// {				
			// 	invalid_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;
			// 	gc_eject_count = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].strip_page;
			// 	block=i;						
			// }

			if((active_block!=i)&&(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num - ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].strip_page >= invalid_page))						
			{				
				invalid_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num - ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].strip_page;
				block=i;						
			}
			// if((active_block!=i)&&((ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num - ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].strip_page) > invalid_page))						
			// {				
			// 	invalid_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num - ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].strip_page;
			// 	block=i;						
			// }
			i = prei;
			
		}
	}
	if (block==-1)
	{
		// printf("1\n");
		return 1;
	}

	//if(invalid_page<5)
	//{
		//printf("\ntoo less invalid page. \t %d\t %d\t%d\t%d\t%d\t%d\t\n",invalid_page,channel,chip,die,plane,block);
	//}
	//***********************************************
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].fast_erase = TRUE;
	//***********************************************
	free_page=0;
	/*add by winks*/
	if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].write_frequently) {
		//printf("add do not dir %ld\n", ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].changed_count);
		ssd->invaild_page_of_change +=  ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].changed_count;
		ssd->invaild_page_of_all += (ssd->parameter->page_block - ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num);
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].changed_count = 0;
	}
	/*end add by winks*/
	for(i=0;i<ssd->parameter->page_block;i++)		                                                     /*������ÿ��page���������Ч���ݵ�page��Ҫ�ƶ��������ط��洢*/		
	{		
		if ((ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].free_state&PG_SUB)==0x0000000f)
		{
			free_page++;
		}
		if(free_page!=0)
		{
			//printf("\ntoo much free page. \t %d\t .%d\t%d\t%d\t%d\t\n",free_page,channel,chip,die,plane);
		}
		if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state != 0) /*��ҳ����Чҳ����Ҫcopyback����*/		
		{	
			//printf("vaild!!!\n");
			location=(struct local * )malloc(sizeof(struct local ));
			alloc_assert(location,"location");
			memset(location,0, sizeof(struct local));

			location->channel=channel;
			location->chip=chip;
			location->die=die;
			location->plane=plane;
			location->block=block;
			location->page=i;
			if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].lpn == ssd->stripe.checkLpn){
				long long raidid = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].raidID;
				struct buffer_group key;
				key.group = raidid;
				if(((struct buffer_group*)hash_find(ssd->dram->buffer, (HASH_NODE *)&key)) != NULL){
					invaild_page(ssd, location);
					if(ssd->trip2Page[raidid].location->channel != channel || ssd->trip2Page[raidid].location->chip != chip || ssd->trip2Page[raidid].location->die != die || ssd->trip2Page[raidid].location->plane != plane || ssd->trip2Page[raidid].location->block != block || ssd->trip2Page[raidid].location->page != i){
						abort();
					}
					free(ssd->trip2Page[raidid].location);
					ssd->trip2Page[raidid].location = NULL;
					gc_eject(ssd, raidid);
				}else{
					move_page(ssd, location, &transfer_size);                                                   /*��ʵ��move_page����*/
					page_move_count++;
					
					if(gc_node->wl_flag == 0){
						ssd->page_move_count++;
					}else if(gc_node->wl_flag == 1){
						ssd->wl_page_move_count++;
					}
				}
				
				
			}else{
				move_page(ssd, location, &transfer_size);                                                   /*��ʵ��move_page����*/
				page_move_count++;
				
				if(gc_node->wl_flag == 0){
					ssd->page_move_count++;
				}else if(gc_node->wl_flag == 1){
					ssd->wl_page_move_count++;
				}
			}
			free(location);	
			location=NULL;
			
		}else if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].raidID != -1){
			if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].raidID == 9154){
				printf("chan %d chip %d die %d plane %d block %d page %d\n", channel, chip, die, plane, block, i);
			}
			gc_eject(ssd, ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].raidID);
			flag = 1;
		}				
	}

	if(flag == 1){
		ssd->page_move_count_1 += page_move_count;
		++ssd->page_move_count_erase;
	}

	erase_operation(ssd,channel ,chip , die,plane ,block);	                                              /*ִ����move_page�����󣬾�����ִ��block�Ĳ�������*/

	ssd->channel_head[channel].current_state=CHANNEL_GC;									
	ssd->channel_head[channel].current_time=ssd->current_time;										
	ssd->channel_head[channel].next_state=CHANNEL_IDLE;	
	ssd->channel_head[channel].chip_head[chip].current_state=CHIP_ERASE_BUSY;								
	ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;						
	ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;			
	
	/***************************************************************
	*�ڿ�ִ��COPYBACK�߼������벻��ִ��COPYBACK�߼���������������£�
	*channel�¸�״̬ʱ��ļ��㣬�Լ�chip�¸�״̬ʱ��ļ���
	***************************************************************/
	if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)
	{
		if (ssd->parameter->greed_CB_ad==1)
		{

			ssd->channel_head[channel].next_state_predict_time=ssd->current_time+page_move_count*(7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tPROG);			
			ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tBERS;
		} 
	} 
	else
	{

		ssd->channel_head[channel].next_state_predict_time=ssd->current_time+page_move_count*(7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tPROG)+transfer_size*SECTOR*(ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tRC);
		//printf("transfer_size:%f\n",transfer_size);	
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tBERS;

	}
	if(ssd->gcIntervalFile)
		fprintf(ssd->gcIntervalFile,"%lld\t%lld\t%lld\n", ssd->current_time, ssd->channel_head[channel].chip_head[chip].next_state_predict_time, ssd->current_time - ssd->preRequestArriveTime[ssd->preRequestArriveTimeIndex]);
	return 1;
}

int uninterrupt_fast_gc(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane,unsigned int priority)       
{
	//printf("===>Enter uninterrupt_fast_gc.\n");
	unsigned int i=0,invalid_page=0;
	unsigned int block,block1,active_block,transfer_size,free_page,page_move_count=0;                           /*��¼ʧЧҳ���Ŀ��*/
	struct local *  location=NULL;
	unsigned int total_invalid_page_num=0;
	
	block1 = -1;
	block = 0;
	struct plane_info candidate_plane;
	candidate_plane = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane];
	unsigned int invalid_lsb_num = 0;
	while(block < ssd->parameter->block_plane-1){
		//printf("Enter while, %d\n", block);
		if((candidate_plane.blk_head[block].free_msb_num==ssd->parameter->page_block/2)&&(candidate_plane.blk_head[block].fast_erase!=TRUE)){
			if(candidate_plane.blk_head[block].free_lsb_num==0 && candidate_plane.blk_head[block].invalid_lsb_num>invalid_lsb_num){
				block1 = block;
				invalid_lsb_num = candidate_plane.blk_head[block].invalid_lsb_num;
				}
			}
		block++;
		}
	if (block1==-1)
	{
		//printf("====>No block has invalid LSB page??\n");
		return SUCCESS;
	}
	if(priority==GC_FAST_UNINTERRUPT_EMERGENCY){
		if(candidate_plane.blk_head[block1].invalid_lsb_num < ssd->parameter->page_block/2*ssd->parameter->fast_gc_filter_2){
			return SUCCESS;
			}
		}
	else if(priority==GC_FAST_UNINTERRUPT){
		if(candidate_plane.blk_head[block1].invalid_lsb_num < ssd->parameter->page_block/2*ssd->parameter->fast_gc_filter_1){
			return SUCCESS;
			}
		}
	else if(priority==GC_FAST_UNINTERRUPT_IDLE){
		if(candidate_plane.blk_head[block1].invalid_lsb_num < ssd->parameter->page_block/2*ssd->parameter->fast_gc_filter_idle){
			return SUCCESS;
			}
		//printf("FAST_GC_IDLE on %d, %d, %d, %d, %d.\n", channel, chip, die, plane, block1);
		ssd->idle_fast_gc_count++;
		}
	else{
		printf("GC_FAST ERROR. PARAMETERS WRONG.\n");
		return SUCCESS;
		}
	
	free_page=0;
	struct blk_info candidate_block;
	candidate_block = candidate_plane.blk_head[block1];
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block1].fast_erase = TRUE;
	//printf("%d %d %d %d %d is selected, free_msbs:%d, invalid_lsbs:%d.\n",channel,chip,die,plane,block1, candidate_block.free_msb_num, candidate_block.invalid_lsb_num);
	//printf("begin to move pages.\n");
	//printf("<<<Fast GC Candidate, %d, %d, %d, %d, %d.\n",channel,chip,die,plane,block1);
	//printf("Begin to MOVE PAGES.\n");
	for(i=0;i<ssd->parameter->page_block;i++)		                                                     /*������ÿ��page���������Ч���ݵ�page��Ҫ�ƶ��������ط��洢*/		
	{		
		if(candidate_block.page_head[i].valid_state!=0) /*��ҳ����Чҳ����Ҫcopyback����*/		
		{	
			//printf("move page %d.\n",i);
			location=(struct local * )malloc(sizeof(struct local ));
			alloc_assert(location,"location");
			memset(location,0, sizeof(struct local));

			location->channel=channel;
			location->chip=chip;
			location->die=die;
			location->plane=plane;
			location->block=block1;
			location->page=i;
			//printf("state of this block: %d.\n",candidate_block.fast_erase);
			move_page(ssd, location, &transfer_size);                                                   /*��ʵ��move_page����*/
			page_move_count++;

			free(location);	
			location=NULL;
		}				
	}
	//printf("block: %d, %d, %d, %d, %d going to be erased.\n",channel, chip, die, plane, block1);
	erase_operation(ssd,channel ,chip , die, plane ,block1);	                                              /*ִ����move_page�����󣬾�����ִ��block�Ĳ�������*/
	//printf("ERASE OPT: %d, %d, %d, %d, %d\n",channel,chip,die,plane,block1);
	ssd->channel_head[channel].current_state=CHANNEL_GC;									
	ssd->channel_head[channel].current_time=ssd->current_time;										
	ssd->channel_head[channel].next_state=CHANNEL_IDLE;	
	ssd->channel_head[channel].chip_head[chip].current_state=CHIP_ERASE_BUSY;								
	ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;						
	ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;			

	ssd->fast_gc_count++;
	
	/***************************************************************
	*�ڿ�ִ��COPYBACK�߼������벻��ִ��COPYBACK�߼���������������£�
	*channel�¸�״̬ʱ��ļ��㣬�Լ�chip�¸�״̬ʱ��ļ���
	***************************************************************/
	if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)
	{
		if (ssd->parameter->greed_CB_ad==1)
		{

			ssd->channel_head[channel].next_state_predict_time=ssd->current_time+page_move_count*(7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tPROG);			
			ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tBERS;
		} 
	} 
	else
	{

		ssd->channel_head[channel].next_state_predict_time=ssd->current_time+page_move_count*(7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tPROG)+transfer_size*SECTOR*(ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tRC);					
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tBERS;

	}
	//printf("===>Exit uninterrupt_fast_gc Successfully.\n");
	return 1;
}

int uninterrupt_dr(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane)       
{
	unsigned int i=0,invalid_page=0;
	unsigned int block,active_block,transfer_size,free_page,page_move_count=0;                           /*��¼ʧЧҳ���Ŀ��*/
	struct local *  location=NULL;
	unsigned int total_invalid_page_num=0;
	/*
	if(find_active_block_dr(ssd,channel,chip,die,plane)!=SUCCESS)
	{
		printf("\n\n Error in uninterrupt_dr(). No active block\n");
		return ERROR;
	}
	active_block=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
	*/
	invalid_page=0;
	transfer_size=0;
	block=-1;
	for(i=0;i<ssd->parameter->block_plane;i++)                                                           /*�������invalid_page�Ŀ�ţ��Լ�����invalid_page_num*/
	{	
		total_invalid_page_num+=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;
		if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num>invalid_page)						
		{				
			invalid_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;
			block=i;						
		}
	}
	
	if (block==-1)
	{
		printf("No block is selected.\n");
		return 1;
	}
	//��Ŀ��block��dr_state��Ϊ���
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].dr_state=DR_STATE_OUTPUT;

	
	//printf("Block %d with %d invalid pages is selected.\n", block, ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num);
	free_page=0;
	for(i=0;i<ssd->parameter->page_block;i++)		                                                     /*������ÿ��page���������Ч���ݵ�page��Ҫ�ƶ��������ط��洢*/		
	{		
		if ((ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].free_state&PG_SUB)==0x0000000f)
		{
			free_page++;
		}
		/*
		if(free_page!=0)
		{
			//printf("\ntoo much free page. \t %d\t .%d\t%d\t%d\t%d\t\n",free_page,channel,chip,die,plane);
		}
		*/
		if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state!=0) /*��ҳ����Чҳ����Ҫcopyback����*/		
		{	
			location=(struct local * )malloc(sizeof(struct local ));
			alloc_assert(location,"location");
			memset(location,0, sizeof(struct local));

			location->channel=channel;
			location->chip=chip;
			location->die=die;
			location->plane=plane;
			location->block=block;
			location->page=i;
			if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].dr_state != DR_STATE_OUTPUT){
				printf("The DR_STATE is not settle.\n");
				}
			move_page(ssd, location, &transfer_size);                                                   /*��ʵ��move_page����*/
			page_move_count++;

			free(location);	
			location=NULL;
		}				
	}
	erase_operation(ssd,channel ,chip , die,plane ,block);	                                              /*ִ����move_page�����󣬾�����ִ��block�Ĳ�������*/
	/*
	ssd->channel_head[channel].current_state=CHANNEL_GC;									
	ssd->channel_head[channel].current_time=ssd->current_time;										
	ssd->channel_head[channel].next_state=CHANNEL_IDLE;	
	ssd->channel_head[channel].chip_head[chip].current_state=CHIP_ERASE_BUSY;								
	ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;						
	ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;			
	*/
	/***************************************************************
	*�ڿ�ִ��COPYBACK�߼������벻��ִ��COPYBACK�߼���������������£�
	*channel�¸�״̬ʱ��ļ��㣬�Լ�chip�¸�״̬ʱ��ļ���
	***************************************************************/
	/*
	if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)
	{
		if (ssd->parameter->greed_CB_ad==1)
		{

			ssd->channel_head[channel].next_state_predict_time=ssd->current_time+page_move_count*(7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tPROG);			
			ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tBERS;
		} 
	} 
	else
	{

		ssd->channel_head[channel].next_state_predict_time=ssd->current_time+page_move_count*(7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tPROG)+transfer_size*SECTOR*(ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tRC);					
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tBERS;

	}
	*/
	return 1;
}


/*******************************************************************************************************************************************
*Ŀ���planeû�п���ֱ��ɾ����block����ҪѰ��Ŀ����������ʵʩ�������������ڿ����жϵ�gc�����У��ɹ�ɾ��һ���飬����1��û��ɾ��һ���鷵��-1
*����������У����ÿ���Ŀ��channel,die�Ƿ��ǿ��е�
********************************************************************************************************************************************/
int interrupt_gc(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane,struct gc_operation *gc_node)        
{
	unsigned int i,block=gc_node->block,active_block,transfer_size,invalid_page=0;
	struct local *location;

	active_block=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
	transfer_size=0;
	if (gc_node->block>=ssd->parameter->block_plane)
	{
		for(i=0;i<ssd->parameter->block_plane;i++)
		{			
			if((active_block!=i)&&(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num>invalid_page))						
			{				
				invalid_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;
				block=i;						
			}
		}
		if (!invalid_page) return 0;
        gc_node->block=block;
	}

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[gc_node->block].invalid_page_num!=ssd->parameter->page_block)     /*����Ҫִ��copyback����*/
	{
		for (i=gc_node->page;i<ssd->parameter->page_block;i++)
		{
			if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[gc_node->block].page_head[i].valid_state!=0)
			{
				location=(struct local * )malloc(sizeof(struct local ));
				alloc_assert(location,"location");
				memset(location,0, sizeof(struct local));

				location->channel=channel;
				location->chip=chip;
				location->die=die;
				location->plane=plane;
				location->block=gc_node->block;
				location->page=i;
				transfer_size=0;

				move_page( ssd, location, &transfer_size);

				free(location);
				location=NULL;

				gc_node->page=i+1;
				/* move_page already increments invalid_page_num. */
				ssd->channel_head[channel].current_state=CHANNEL_C_A_TRANSFER;									
				ssd->channel_head[channel].current_time=ssd->current_time;										
				ssd->channel_head[channel].next_state=CHANNEL_IDLE;	
				ssd->channel_head[channel].chip_head[chip].current_state=CHIP_COPYBACK_BUSY;								
				ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;						
				ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;		

				if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)
				{					
					ssd->channel_head[channel].next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+7*ssd->parameter->time_characteristics.tWC;		
					ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;
				} 
				else
				{	
					ssd->channel_head[channel].next_state_predict_time=ssd->current_time+(7+transfer_size*SECTOR)*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(7+transfer_size*SECTOR)*ssd->parameter->time_characteristics.tWC;					
					ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;
				}
				return 0;    
			}
		}
	}
	else
	{
		erase_operation(ssd,channel ,chip, die,plane,gc_node->block);	

		ssd->channel_head[channel].current_state=CHANNEL_C_A_TRANSFER;									
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;								
		ssd->channel_head[channel].next_state_predict_time=ssd->current_time+5*ssd->parameter->time_characteristics.tWC;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_ERASE_BUSY;								
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;						
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;							
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tBERS;

		return 1;                                                                      /*��gc������ɣ�����1�����Խ�channel�ϵ�gc����ڵ�ɾ��*/
	}

	printf("there is a problem in interrupt_gc\n");
	return 1;
}

/*************************************************************
*�����Ĺ����ǵ�������һ��gc����ʱ����Ҫ��gc���ϵ�gc_nodeɾ����
**************************************************************/
int delete_gc_node(struct ssd_info *ssd, unsigned int channel,struct gc_operation *gc_node)
{
	struct gc_operation *gc_pre=NULL;

	
	if(gc_node==NULL)                                                                  
	{
		return ERROR;
	}

	if (gc_node==ssd->channel_head[channel].gc_command)
	{
		ssd->channel_head[channel].gc_command=gc_node->next_node;
		/*add by winks*/
		if(gc_node == ssd->channel_head[channel].last_gc_command)
			ssd->channel_head[channel].last_gc_command = NULL;
		/*end add by winks*/
	}
	else
	{
		gc_pre=ssd->channel_head[channel].gc_command;
		while (gc_pre->next_node!=NULL)
		{
			if (gc_pre->next_node==gc_node)
			{
				gc_pre->next_node=gc_node->next_node;
				/*add by winks*/
				if(gc_node == ssd->channel_head[channel].last_gc_command)
					ssd->channel_head[channel].last_gc_command = gc_pre;
				/*end add by winks*/
				break;
			}
			gc_pre=gc_pre->next_node;
		}
	}
	free(gc_node);
	gc_node=NULL;
	ssd->gc_request--;
	return SUCCESS;
}

/***************************************
*��������Ĺ����Ǵ���channel��ÿ��gc����
****************************************/
Status gc_for_channel(struct ssd_info *ssd, unsigned int channel)
{
	int flag_direct_erase=1,flag_gc=1,flag_invoke_gc=1;
	unsigned int chip,die,plane,flag_priority=0;
	unsigned int current_state=0, next_state=0;
	long long next_state_predict_time=0;
	struct gc_operation *gc_node=NULL,*gc_p=NULL;
	struct sub_request* sub;

	/*******************************************************************************************
	*����ÿһ��gc_node����ȡgc_node���ڵ�chip�ĵ�ǰ״̬���¸�״̬���¸�״̬��Ԥ��ʱ��
	*�����ǰ״̬�ǿ��У������¸�״̬�ǿ��ж��¸�״̬��Ԥ��ʱ��С�ڵ�ǰʱ�䣬�����ǲ����жϵ�gc
	*��ô��flag_priority��Ϊ1������Ϊ0
	********************************************************************************************/
	int i = (channel % 4) * 4;
	int end = i + 4;
	/*for(; i < end; ++i){
		int chan = i % ssd->parameter->channel_number;
		if(ssd->channel_head[chan].current_state == CHANNEL_GC && ssd->channel_head[chan].next_state_predict_time > ssd->current_time){
			break;
		}	
	}
	if(i != end)
		return FAILURE;*/
	/*sub = ssd->channel_head[channel].subs_r_head;
	while(sub){
		if(sub->degradeRead){
			if((sub->current_state == SR_COMPLETE)||((sub->next_state==SR_COMPLETE)&&(sub->next_state_predict_time<=ssd->current_time))){

			}else{
				return FAILURE;
			}
		}
		sub = sub=sub->next_node;
	}*/
	
	
	gc_node=ssd->channel_head[channel].gc_command;

	while (gc_node!=NULL)
	{
		current_state=ssd->channel_head[channel].chip_head[gc_node->chip].current_state;
		next_state=ssd->channel_head[channel].chip_head[gc_node->chip].next_state;
		next_state_predict_time=ssd->channel_head[channel].chip_head[gc_node->chip].next_state_predict_time;
		if((current_state==CHIP_IDLE)||((next_state==CHIP_IDLE)&&(next_state_predict_time<=ssd->current_time)))
		{
			if (gc_node->priority==GC_UNINTERRUPT)                                     /*���gc�����ǲ����жϵģ����ȷ������gc����*/
			{
				if (ssd->channel_head[channel].chip_head[gc_node->chip].die_head[gc_node->die].plane_head[gc_node->plane].free_page < (ssd->parameter->page_block*ssd->parameter->block_plane*(ssd->parameter->gc_hard_threshold))) {
					flag_priority=1;
					break;
				}
			}
		}
		gc_node=gc_node->next_node;
	}
	if (flag_priority!=1)                                                              /*û���ҵ������жϵ�gc��������ִ�ж��׵�gc����*/
	{
		gc_node=ssd->channel_head[channel].gc_command;
		while (gc_node!=NULL)
		{
			current_state=ssd->channel_head[channel].chip_head[gc_node->chip].current_state;
			next_state=ssd->channel_head[channel].chip_head[gc_node->chip].next_state;
			next_state_predict_time=ssd->channel_head[channel].chip_head[gc_node->chip].next_state_predict_time;
			 /**********************************************
			 *��Ҫgc������Ŀ��chip�ǿ��еģ��ſ��Խ���gc����
			 ***********************************************/
			if((current_state==CHIP_IDLE)||((next_state==CHIP_IDLE)&&(next_state_predict_time<=ssd->current_time)))   
			{
				break;
			}
			gc_node=gc_node->next_node;
		}

	}
	if(gc_node==NULL)
	{
		return FAILURE;
	}

	chip=gc_node->chip;
	die=gc_node->die;
	plane=gc_node->plane;
	
	if (gc_node->priority==GC_UNINTERRUPT)
	{
		flag_direct_erase=gc_direct_erase(ssd,channel,chip,die,plane);
		if (flag_direct_erase!=SUCCESS)
		{
			
			flag_gc=uninterrupt_gc(ssd,channel,chip,die,plane,gc_node);                         /*��һ��������gc�������ʱ���Ѿ�����һ���飬������һ��������flash�ռ䣩������1����channel����Ӧ��gc��������ڵ�ɾ��*/
			if (flag_gc==1)
			{
				delete_gc_node(ssd,channel,gc_node);
			}
		}
		else
		{
			delete_gc_node(ssd,channel,gc_node);
		}
		return SUCCESS;
	}
	/*******************************************************************************
	*���жϵ�gc������Ҫ����ȷ�ϸ�channel��û�������������ʱ����Ҫʹ�����channel��
	*û�еĻ�����ִ��gc�������еĻ�����ִ��gc����
	********************************************************************************/
	else if(gc_node->priority==GC_FAST_UNINTERRUPT || gc_node->priority==GC_FAST_UNINTERRUPT_EMERGENCY || gc_node->priority==GC_FAST_UNINTERRUPT_IDLE){
		//printf("===>GC_FAST on %d,%d,%d,%d begin.\n",channel,chip,die,plane);
		flag_direct_erase=gc_direct_fast_erase(ssd,channel,chip,die,plane);
		if (flag_direct_erase!=SUCCESS)
		{
			/*
			printf("Something Weird happened.\n");
			return FAILURE;
			*/
			//printf("NO BLOCK CAN BE ERASED DIRECTLY.\n");
			flag_gc=uninterrupt_fast_gc(ssd,channel,chip,die,plane,gc_node->priority);
			if (flag_gc==1)
			{
				delete_gc_node(ssd,channel,gc_node);
			}
		}
		else
		{
			//printf("THERE IS BLOCK CAN BE ERASED DIRECTLY.\n");
			delete_gc_node(ssd,channel,gc_node);
		}
		//printf("===>GC_FAST on %d,%d,%d,%d successed.\n",channel,chip,die,plane);
		return SUCCESS;
		}
	else        
	{
		flag_invoke_gc=decide_gc_invoke(ssd,channel);                                  /*�ж��Ƿ�����������Ҫchannel���������������Ҫ���channel����ô���gc�����ͱ��ж���*/

		if (flag_invoke_gc==1)
		{
			flag_direct_erase=gc_direct_erase(ssd,channel,chip,die,plane);
			if (flag_direct_erase==-1)
			{
				flag_gc=interrupt_gc(ssd,channel,chip,die,plane,gc_node);             /*��һ��������gc�������ʱ���Ѿ�����һ���飬������һ��������flash�ռ䣩������1����channel����Ӧ��gc��������ڵ�ɾ��*/
				if (flag_gc==1)
				{
					delete_gc_node(ssd,channel,gc_node);
				}
			}
			else if (flag_direct_erase==1)
			{
				delete_gc_node(ssd,channel,gc_node);
			}
			return SUCCESS;
		} 
		else
		{
			return FAILURE;
		}		
	}
}

Status find_invalid_block(struct ssd_info *ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane){
	unsigned int block, flag;
	for(block=0;block<ssd->parameter->block_plane;block++){
		if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num>(ssd->parameter->page_block*ssd->parameter->dr_filter)){
			return TRUE;
			}
		}
	return FALSE;
}

Status dr_for_channel(struct ssd_info *ssd, unsigned int channel)
{
	int flag_direct_erase=1,flag_gc=1,flag_invoke_gc=1;
	unsigned int chip,die,plane,flag_priority=0;
	unsigned int current_state=0, next_state=0;
	long long next_state_predict_time=0;
	struct gc_operation *gc_node=NULL,*gc_p=NULL;

	/*******************************************************************************************
	*����ÿһ��gc_node����ȡgc_node���ڵ�chip�ĵ�ǰ״̬���¸�״̬���¸�״̬��Ԥ��ʱ��
	*�����ǰ״̬�ǿ��У������¸�״̬�ǿ��ж��¸�״̬��Ԥ��ʱ��С�ڵ�ǰʱ�䣬�����ǲ����жϵ�gc
	*��ô��flag_priority��Ϊ1������Ϊ0
	********************************************************************************************/
	//======================================================================
	//�����������ղ����������
	ssd->channel_head[channel].gc_command=NULL;
	gc_node=ssd->channel_head[channel].gc_command;
	//��ѯ����plane��������Чҳ��block�����յ�
	unsigned int plane_flag;
	unsigned int counter;
	for(chip=0;chip<ssd->parameter->chip_channel[0];chip++){
		for(die=0;die<ssd->parameter->die_chip;die++){
			for(plane=0;plane<ssd->parameter->plane_die;plane++){
				plane_flag = find_invalid_block(ssd, channel, chip, die, plane);
				counter=0;
				while(plane_flag==TRUE){
					counter++;
					//printf("There is data should be reorganize in %d, %d, %d, %d.\n",channel, chip, die, plane);
					uninterrupt_dr(ssd, channel, chip, die, plane);
					plane_flag = find_invalid_block(ssd, channel, chip, die, plane);
					}
				printf("SUCCESS. Data in %d, %d, %d, %d is reorganized.\n",channel, chip, die, plane);
				}
			}
    	}
	return SUCCESS;
}


/************************************************************************************************************
*flag�������gc��������ssd��������idle������±����õģ�1��������ȷ����channel��chip��die��plane�����ã�0��
*����gc��������Ҫ�ж��Ƿ��ǲ����жϵ�gc����������ǣ���Ҫ��һ����Ŀ��block��ȫ�����������ɣ�����ǿ��жϵģ�
*�ڽ���GC����ǰ����Ҫ�жϸ�channel��die�Ƿ����������ڵȴ����������û����ʼһ��һ���Ĳ������ҵ�Ŀ��
*���һ��ִ��һ��copyback����������gc��������ʱ����ǰ�ƽ���������һ��copyback����erase����
*����gc������һ����Ҫ����gc��������Ҫ����һ�����жϣ�������Ӳ��ֵ����ʱ���������gc����������������ֵ����ʱ��
*��Ҫ�жϣ������channel���Ƿ����������ڵȴ�(��д������ȴ��Ͳ��У�gc��Ŀ��die����busy״̬Ҳ����)�����
*�оͲ�ִ��gc���������������ִ��һ������
************************************************************************************************************/
unsigned int gc(struct ssd_info *ssd,unsigned int channel, unsigned int flag)
{
	unsigned int i;
	int flag_direct_erase=1,flag_gc=1,flag_invoke_gc=1;
	unsigned int flag_priority=0, ret=FAILURE;
	struct gc_operation *gc_node=NULL,*gc_p=NULL;
	

	if (flag==1)                                                                       /*����ssd����IDEL�����*/
	{
		for (i=0;i<ssd->parameter->channel_number;i++)
		{
			flag_priority=0;
			flag_direct_erase=1;
			flag_gc=1;
			flag_invoke_gc=1;
			gc_node=NULL;
			gc_p=NULL;
			if((ssd->channel_head[i].current_state==CHANNEL_IDLE)||(ssd->channel_head[i].next_state==CHANNEL_IDLE&&ssd->channel_head[i].next_state_predict_time<=ssd->current_time))
			{
				channel=i;
				if (ssd->channel_head[channel].gc_command!=NULL)
				{
					ret = gc_for_channel(ssd, channel);
				}
			}
		}
		return ret;

	} 
	else                                                                               /*ֻ�����ĳ���ض���channel��chip��die����gc����Ĳ���(ֻ���Ŀ��die�����ж������ǲ���idle��*/
	{
		if ((ssd->parameter->allocation_scheme==1)||((ssd->parameter->allocation_scheme==0)&&(ssd->parameter->dynamic_allocation==1)))
		{
			// (don`t into this)
			if ((ssd->channel_head[channel].subs_r_head!=NULL)||(ssd->channel_head[channel].subs_w_head!=NULL))    /*�������������ȷ�������*/
			{
				return 0;
			}
		}
	
		ret = gc_for_channel(ssd,channel);
		return ret;
	}
}



/**********************************************************
*�ж��Ƿ���������Ѫҩchannel������û�з���1�Ϳ��Է���gc����
*����з���0���Ͳ���ִ��gc������gc�������ж�
***********************************************************/
int decide_gc_invoke(struct ssd_info *ssd, unsigned int channel)      
{
	struct sub_request *sub;
	struct local *location;

	if ((ssd->channel_head[channel].subs_r_head==NULL)&&(ssd->channel_head[channel].subs_w_head==NULL))    /*������Ҷ�д�������Ƿ���Ҫռ�����channel�����õĻ�����ִ��GC����*/
	{
		return 1;                                                                        /*��ʾ��ǰʱ�����channelû����������Ҫռ��channel*/
	}
	else
	{
		if (ssd->channel_head[channel].subs_w_head!=NULL)
		{
			return 0;
		}
		else if (ssd->channel_head[channel].subs_r_head!=NULL)
		{
			sub=ssd->channel_head[channel].subs_r_head;
			while (sub!=NULL)
			{
				if (sub->current_state==SR_WAIT)                                         /*����������Ǵ��ڵȴ�״̬���������Ŀ��die����idle������ִ��gc����������0*/
				{
					location=find_location(ssd,sub->ppn);
					if ((ssd->channel_head[location->channel].chip_head[location->chip].current_state==CHIP_IDLE)||((ssd->channel_head[location->channel].chip_head[location->chip].next_state==CHIP_IDLE)&&
						(ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time<=ssd->current_time)))
					{
						free(location);
						location=NULL;
						return 0;
					}
					free(location);
					location=NULL;
				}
				else if (sub->next_state==SR_R_DATA_TRANSFER)
				{
					location=find_location(ssd,sub->ppn);
					if (ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time<=ssd->current_time)
					{
						free(location);
						location=NULL;
						return 0;
					}
					free(location);
					location=NULL;
				}
				sub=sub->next_node;
			}
		}
		return 1;
	}
}

