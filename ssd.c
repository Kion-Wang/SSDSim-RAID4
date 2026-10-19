/*****************************************************************************************************************************
This project was supported by the National Basic Research 973 Program of China under Grant No.2011CB302301
Huazhong University of Science and Technology (HUST)   Wuhan National Laboratory for Optoelectronics

FileName�� ssd.c
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

 

#include "ssd.h"
#include "hash.h"
#include "raid4.h"
#include <unistd.h>

int index1 = 0, index2 = 0, index3 = 0, RRcount = 0;
float aveber=0;

/********************************************************************************************************************************
1��main������initiatio()����������ʼ��ssd,��2��makesimulating 0 times_aged()����ʹSSD��Ϊaged��aged��ssd�൱��ʹ�ù�һ��ʱ���ssd��������ʧЧҳ��
non_aged��ssd���µ�ssd����ʧЧҳ��ʧЧҳ�ı��������ڳ�ʼ�����������ã�3��pre_process_page()������ǰɨһ������󣬰Ѷ�����
��lpn<--->ppnӳ���ϵ���Ƚ����ã�д�����lpn<--->ppnӳ���ϵ��д��ʱ���ٽ�����Ԥ����trace��ֹ�������Ƕ��������ݣ�4��simulate()��
���Ĵ���������trace�ļ��Ӷ�������������ɶ��������������ɣ�5��statistic_output()������ssd�ṹ�е���Ϣ���������ļ����������
ͳ�����ݺ�ƽ�����ݣ�����ļ���С��trace_output�ļ���ܴ����ϸ��6��free_all_node()�����ͷ�����main����������Ľڵ�
*********************************************************************************************************************************/

void init_valid(struct ssd_info *ssd){
		unsigned int i, j, k, l, m, n;	
		unsigned int lpn = 0;
		unsigned int lsn = 100000;
		unsigned int ppn, full_page = FULL_PAGE_STATE(ssd->parameter->subpage_page);
		int sub_size = 8;
		//init valid pages
		for (i = 0; i < ssd->parameter->channel_number; i++)
		{
			for (j = 0; j < ssd->parameter->chip_num / ssd->parameter->channel_number; j++)
			{
				for (k = 0; k < ssd->parameter->die_chip; k++)
				{
					for (l = 0; l < ssd->parameter->plane_die; l++)
					{
						for (m = 0; m < ssd->parameter->block_plane; m++)
						{
							for (n = 0; n < 0.30 * ssd->parameter->page_block; n++)
							{
	
								lpn = lsn / ssd->parameter->subpage_page;
								ppn = find_ppn(ssd, i, j, k, l, m, n);
								//modify state
								ssd->dram->map->map_entry[lpn].pn = ppn;
								ssd->dram->map->map_entry[lpn].state = set_entry_state(ssd, lsn, sub_size);   //0001
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].page_head[n].lpn = lpn;
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].page_head[n].valid_state = ssd->dram->map->map_entry[lpn].state;
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].page_head[n].free_state = ((~ssd->dram->map->map_entry[lpn].state) & full_page);
								//--
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].last_write_page++;
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].free_page_num--;
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].free_page--;
								if(ppn % 3 == 0)
									ssd->free_lsb_count--;
								if(ppn % 3 == 2)
									ssd->free_msb_count--;
								else 
									ssd->free_csb_count--;
								lsn++;
							}
						}
	
					}
				}
			}
		}
	

}

void raid_init(struct ssd_info *ssd){
	unsigned long long pageNum = ((ssd->parameter->page_block)*(ssd->parameter->block_plane)*(ssd->parameter->plane_die)*(ssd->parameter->die_chip)*(ssd->parameter->chip_channel[0])*(ssd->parameter->channel_number));
	unsigned long long i;
	unsigned long long StripeNum = pageNum / 4;
	unsigned long long chipNum = ssd->parameter->chip_channel[0] * ssd->parameter->channel_number;
	
	ssd->perChanSSD = 4;
	ssd->ssdToken = 0;

	ssd->page2Trip = (long long*)malloc(pageNum * sizeof(unsigned long long));
	alloc_assert(ssd->page2Trip, "ssd->page2Trip");
	for(i = 0; i < pageNum; i++)
		ssd->page2Trip[i] = -1;

	
	ssd->stripe.all = 4;
	StripeNum = pageNum / ssd->stripe.all;
	ssd->stripe.now = 0;
	ssd->stripe.check = 3;
	ssd->stripe.checkLpn = ((ssd->parameter->subpage_page*ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*(ssd->parameter->chip_num)));
	ssd->stripe.checkLpn /= ssd->parameter->subpage_page;
	/* checkLpn is a sentinel only: never index the data map with it. */

	ssd->stripe.checkStart = ssd->stripe.checkLpn;
	ssd->stripe.checkStart -= (ssd->stripe.checkStart / ssd->stripe.all);
	ssd->stripe.checkStart *= (1 - ssd->parameter->overprovide);
    ssd->stripe.checkStart -= ssd->stripe.checkStart % 3;
	
	

	ssd->stripe.nowStripe = 0;
	ssd->stripe.allStripe = pageNum / ssd->stripe.all + 1;
	
	ssd->stripe.req = (struct StripeReq*)malloc(sizeof(struct StripeReq) * ssd->stripe.all);
	alloc_assert(ssd->stripe.req, "ssd->stripe.req");
	memset(ssd->stripe.req , 0, sizeof(struct StripeReq) * ssd->stripe.all);

	ssd->raidReq = malloc(sizeof(struct request));
	alloc_assert(ssd->raidReq, "ssd->raidReq");
	memset(ssd->raidReq, 0, sizeof(struct request));
	ssd->raidReq->subs = NULL;

	
	ssd->trip2Page = (struct RaidInfo*)malloc(ssd->stripe.allStripe * sizeof(struct RaidInfo));
	alloc_assert(ssd->trip2Page, "ssd->trip2Page");
	memset(ssd->trip2Page, 0, ssd->stripe.allStripe * sizeof(struct RaidInfo));

	//abort();
	long long *offset = malloc(ssd->stripe.all * sizeof(long long) * ssd->stripe.allStripe);
	for(i = 0; i < ssd->stripe.allStripe; i++){
		//printf("i %d\n", i);
		ssd->trip2Page[i].lpn = offset;
		offset += ssd->stripe.all;
		memset(ssd->trip2Page[i].lpn, 0, ssd->stripe.all * sizeof(long long));
		ssd->trip2Page[i].check = NULL;
		/*alloc_assert(ssd->trip2Page[i].check, "ssd->trip2Page[i].check");*/
		//memset(ssd->trip2Page[i].check, 0, 4 * sizeof(unsigned int));
		//offset += 4;
		ssd->trip2Page[i].nowChange = 0;
		ssd->trip2Page[i].allChange = 0;
		ssd->trip2Page[i].using = 0;
		ssd->trip2Page[i].location = NULL;
		ssd->trip2Page[i].readFlag = 0;
		ssd->trip2Page[i].Pchannel = -1;
	}
	
	ssd->raidBitMap = malloc((pageNum + sizeof(unsigned char) * 8 - 1) / 8);
	alloc_assert(ssd->raidBitMap, "ssd->trip2Page");
	memset(ssd->raidBitMap, 0, (pageNum + sizeof(unsigned char) * 8 - 1) / 8);

	
	ssd->chipWrite = malloc(sizeof(unsigned long long) * chipNum);
	alloc_assert(ssd->chipWrite, "ssd->chipWrite");
	for(i = 0; i < ssd->parameter->chip_channel[0] * ssd->parameter->channel_number; ++i){
		ssd->chipWrite[i] = 0;
	}

	ssd->chipGc = malloc(sizeof(unsigned long long) * chipNum);
	alloc_assert(ssd->chipGc, "ssd->chipGc");
	for(i = 0; i < ssd->parameter->chip_channel[0] * ssd->parameter->channel_number; ++i){
		ssd->chipGc[i] = 0;
	}

	ssd->preRequestArriveTimeIndexMAX = 16;
	ssd->preRequestArriveTimeIndex = 0;
	ssd->preRequestArriveTime = malloc(sizeof(unsigned long long) * ssd->preRequestArriveTimeIndexMAX);
	alloc_assert(ssd->preRequestArriveTime, "ssd->preRequestArriveTime");
	memset(ssd->preRequestArriveTime, 0, sizeof(unsigned long long) * ssd->preRequestArriveTimeIndexMAX);


	ssd->chanSubLenAll = malloc(sizeof(unsigned long long ) * ssd->parameter->channel_number);
	alloc_assert(ssd->chanSubLenAll, "ssd->chanSubWLenAll");
	memset(ssd->chanSubLenAll, 0, sizeof(unsigned long long ) * ssd->parameter->channel_number);

	ssd->chanSubWLenNow = malloc(sizeof(unsigned long long ) * ssd->parameter->channel_number);
	alloc_assert(ssd->chanSubWLenNow, "ssd->chanSubWLenNow");
	memset(ssd->chanSubWLenNow, 0, sizeof(unsigned long long ) * ssd->parameter->channel_number);

	
	ssd->chanSubRLenNow = malloc(sizeof(unsigned long long ) * ssd->parameter->channel_number);
	alloc_assert(ssd->chanSubRLenNow, "ssd->chanSubRLenNow");
	memset(ssd->chanSubRLenNow, 0, sizeof(unsigned long long ) * ssd->parameter->channel_number);

	ssd->chanSubCount = malloc(sizeof(unsigned long long ) * ssd->parameter->channel_number);
	alloc_assert(ssd->chanSubCount, "ssd->chanSubWCount");
	memset(ssd->chanSubCount, 0, sizeof(unsigned long long ) * ssd->parameter->channel_number);

	ssd->chanSubRWriteTime = malloc(sizeof(unsigned long long ) * ssd->parameter->channel_number);
	alloc_assert(ssd->chanSubRWriteTime, "ssd->chanSubRWriteTime");
	memset(ssd->chanSubRWriteTime, 0, sizeof(unsigned long long ) * ssd->parameter->channel_number);

	ssd->preSubWriteLen = 128;
	ssd->preSubWriteNowPos = 0;
	ssd->preSubWriteLpn = malloc(ssd->preSubWriteLen * sizeof(unsigned long long));
	alloc_assert(ssd->preSubWriteLpn, "ssd->preSubWriteLpn");
	memset(ssd->preSubWriteLpn, 0, ssd->preSubWriteLen * sizeof(unsigned long long));

	ssd->paritycount = malloc(ssd->parameter->channel_number * sizeof(unsigned long long));
	alloc_assert(ssd->paritycount, "ssd->paritycount");
	memset(ssd->paritycount, 0, ssd->parameter->channel_number * sizeof(unsigned long long));

	ssd->chanRs = malloc(ssd->parameter->channel_number * sizeof(unsigned long long));
	alloc_assert(ssd->chanRs, "ssd->chanRs");
	memset(ssd->chanRs, 0, ssd->parameter->channel_number * sizeof(unsigned long long));
}

void compute_num(struct ssd_info *ssd){
	int channel = 0;
	int chip = 0;
	if(ssd->completed_request_count > ssd->total_request_num /2){
		int page_plane=0,page_die=0,page_chip=0;
		long long count = 0;
		long long i = 0, j =0;
		page_plane=ssd->parameter->page_block*ssd->parameter->block_plane;
		page_die=page_plane*ssd->parameter->plane_die;
		page_chip=page_die*ssd->parameter->die_chip;

		long long start = (channel * ssd->parameter->chip_channel[0] + chip);
		long long end = start + 1;
		start *= page_chip;
		end *= page_chip;

		for(i = 0; i < ssd->stripe.allStripe; i++){
			if(i >= ssd->stripe.nowStripe){
				break;
			}
			for(j = 0; j < 4; ++j){
				int lpn = ssd->trip2Page[i].lpn[j];
				long long ppn = ssd->dram->map->map_entry[lpn].pn;
				if(ppn >= start && ppn <= end){
					count++;
					break;
				}				
			}
			
		}
		long long bufer_count = 0;
		struct buffer_group *buffer_node = ssd->dram->buffer->buffer_head;
		while(buffer_node){
			if(ssd->dram->map->map_entry[buffer_node->group].state != 0){
				long long ppn = ssd->dram->map->map_entry[buffer_node->group].pn;
				if(ppn >= start && ppn <= end){
					bufer_count++;
				}
				
			}
			buffer_node = buffer_node->LRU_link_next;
		}
		printf("count is %lld bufer_count is %lld\n", count, bufer_count);
		exit(0);
	}	

}

void add_invalid_date(struct ssd_info *ssd){
	int lpn = 1;
	unsigned int mask = FULL_PAGE_STATE(ssd->parameter->subpage_page);
	unsigned int full_page;
	if(ssd->parameter->subpage_page == 32){
		full_page = 0xffffffff;
	}
	else{
		full_page=FULL_PAGE_STATE(ssd->parameter->subpage_page);
	}
	ssd->allPage = ((ssd->parameter->page_block)*(ssd->parameter->block_plane)*(ssd->parameter->plane_die)*(ssd->parameter->die_chip)*(ssd->parameter->chip_channel[0])*(ssd->parameter->channel_number));
	int channToken = ssd->channelToken;
	int chipToken = ssd->chipToken;
	int ssdToken = ssd->ssdToken;
	while(ssd->allUsedPage < ssd->allPage * 0.8){
		raid_pre_invalid(ssd);
	}
	ssd->channelToken = channToken;
	ssd->ssdToken = ssdToken;
	ssd->chipToken = chipToken;
}

void add_valid_date(struct ssd_info *ssd){
	int lpn = 1;
	unsigned int mask = FULL_PAGE_STATE(ssd->parameter->subpage_page);
	unsigned int full_page;
	if(ssd->parameter->subpage_page == 32){
		full_page = 0xffffffff;
	}
	else{
		full_page=FULL_PAGE_STATE(ssd->parameter->subpage_page);
	}
	unsigned int start = lpn + 1;
	ssd->allPage = ((ssd->parameter->page_block)*(ssd->parameter->block_plane)*(ssd->parameter->plane_die)*(ssd->parameter->die_chip)*(ssd->parameter->chip_channel[0])*(ssd->parameter->channel_number));
	while(ssd->allUsedPage < ssd->allPage * 0.5){ //0.65
		if(ssd->dram->map->map_entry[lpn].state==0){
			ssd->stripe.req[ssd->stripe.now].lpn = lpn;
			ssd->stripe.req[ssd->stripe.now].state = full_page;				
			ssd->stripe.req[ssd->stripe.now].req = ssd->raidReq;
			ssd->stripe.req[ssd->stripe.now].sub_size = size(full_page);
			ssd->stripe.req[ssd->stripe.now].full_page = full_page;
			if(++ssd->stripe.now == (ssd->stripe.all - 1)){
				//printf("add!!!!\n");
				unsigned int i, j;
				for(i = 0, j = 0; i < ssd->stripe.all; ++i){
					if(i == ssd->stripe.check){
						raid_pre_read(ssd, ssd->stripe.checkLpn * ssd->parameter->subpage_page, ssd->stripe.checkLpn, mask, full_page);
						continue;
					}
					
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
				if(ssd->ssdToken == 0 && ssd->channelToken == 0){
					ssd->stripe.check = 3;
				}
			}	
		}
		lpn += 1000;
		if(lpn >= ssd->stripe.checkStart){
			lpn = start;
			start++;
			if(start ==1000){
				start = 0;
			}
			if(lpn == 1000){
				lpn += 1000;
			}
		}
	}
}

int update_time(struct ssd_info *ssd)  
{  
	unsigned long long nearest_event_time;

	nearest_event_time=find_nearest_event(ssd);
	if (nearest_event_time==MAX_INT64)
	{
		ssd->current_time += 1;    
		return 0;
	}
	else
	{
		if(ssd->current_time == nearest_event_time)
			ssd->current_time += 1;  
		else
			ssd->current_time = nearest_event_time;
		
		return 1;
	}	
}

unsigned long long get_req_num(struct ssd_info *ssd){
	unsigned long long count = 0;
	struct sub_request *sub = ssd->raidReq->subs;
	for(int j=0;j<ssd->parameter->channel_number;j++){
		sub = ssd->channel_head[j].subs_r_head;
		while(sub){
			++count;
			sub = sub->next_node;
		}
		sub = ssd->channel_head[j].subs_w_head;
		while(sub){
			++count;
			sub = sub->next_node;
		}
	}
	sub = ssd->subs_w_head;
	while(sub){
		++count;
		sub = sub->next_node;
	}
	
	return count;

}

int get_recovery_max(struct ssd_info *ssd){
	int ret = 0;
	int pre = 0;
	long long allValid = 0;
	for(int channelIndex = 0; channelIndex < ssd->parameter->channel_number; channelIndex++){
		int now = 0;
		for(int chipIndex = 0; chipIndex < ssd->parameter->chip_channel[0]; ++chipIndex){
			for(int dieIndex = 0; dieIndex < ssd->parameter->die_chip; ++dieIndex){
				for(int planeIndex = 0; planeIndex < ssd->parameter->plane_die; ++planeIndex){
					for(int blockIndex = 0; blockIndex < ssd->parameter->block_plane; ++blockIndex){
						for(int pageIndex = 0; pageIndex < ssd->parameter->page_block; ++pageIndex){
							long long raid = ssd->channel_head[channelIndex].chip_head[chipIndex].die_head[dieIndex].plane_head[planeIndex].blk_head[blockIndex].page_head[pageIndex].raidID;
							if(raid != -1 && ssd->trip2Page[raid].using){
													
							}
							if(ssd->channel_head[channelIndex].chip_head[chipIndex].die_head[dieIndex].plane_head[planeIndex].blk_head[blockIndex].page_head[pageIndex].valid_state){
								allValid++;	
								now++;	
							}
						}
					}
				}
			}
		}

		printf("%d valid %d\n", channelIndex, now);
		
	}
	printf("all valid %lld\n", allValid);
	return ret;
}

unsigned long long get_req_num_read(struct ssd_info *ssd){
	unsigned long long count = 0;
	struct sub_request *sub = ssd->raidReq->subs;
	for(int j=0;j<ssd->parameter->channel_number;j++){
		sub = ssd->channel_head[j].subs_r_head;
		while(sub){
			++count;
			sub = sub->next_node;
		}
	}
	
	return count;

}

unsigned int get_ppn_base_abs(struct ssd_info *ssd, int channel, int chip, int die, int plane, int block, int page){
	unsigned int page_plane=0,page_die=0,page_chip=0,page_channel=0;
	page_plane=ssd->parameter->page_block*ssd->parameter->block_plane; //number of page per plane
	page_die=page_plane*ssd->parameter->plane_die;                     //number of page per die
	page_chip=page_die*ssd->parameter->die_chip;                       //number of page per chip
	page_channel=page_chip*ssd->parameter->chip_channel[0];            //number of page per channel

	return page + ssd->parameter->page_block * block + page_plane * plane +\
			page_die * die + page_chip * chip + page_channel * channel;
}

struct sub_request *get_sub_request_for_recovery_2(struct ssd_info *ssd, int channel, int chip, int die, int plane, int block, int page){
	struct sub_request* sub = (struct sub_request*)malloc(sizeof(struct sub_request));                        /*����һ��������Ľ�???*/
	alloc_assert(sub,"sub_request");
	memset(sub,0, sizeof(struct sub_request));
	struct local *location = (struct local*)malloc(sizeof(struct local));
	alloc_assert(sub,"struct local");
	memset(location,0, sizeof(struct local));

	unsigned int mask=0;
	if(ssd->parameter->subpage_page == 32){
		mask = 0xffffffff;
	}else{
		mask=FULL_PAGE_STATE(ssd->parameter->subpage_page);
	}
	
	location->channel = channel;
	location->chip = chip;
	location->die = die;
	location->plane = plane;
	location->block = block;
	location->page = page;
	
	sub->location = location;
	sub->lpn = ssd->stripe.checkLpn + 1;
	sub->size = size(mask);
	sub->ppn = get_ppn_base_abs(ssd, channel, chip, die, plane, block, page);
	sub->state=mask;
	sub->raidNUM = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].raidID;

	sub->next_subs = ssd->raidReq->subs;
	ssd->raidReq->subs = sub;
	
	return sub;
}

void create_recovery_sub_read_2(struct ssd_info *ssd, int channel, int chip, int die, int plane, int block, int page){
	struct sub_request* sub = get_sub_request_for_recovery_2(ssd, channel, chip, die, plane, block, page);
		
	sub->begin_time = ssd->current_time;
	sub->current_state = SR_WAIT;
	sub->current_time=MAX_INT64;
	sub->next_state = SR_R_C_A_TRANSFER;
	sub->next_state_predict_time=MAX_INT64;
	
	sub->operation = READ;

	struct channel_info *p_ch = &ssd->channel_head[channel];
	if (p_ch->subs_r_tail!=NULL){
		p_ch->subs_r_tail->next_node=sub;
		p_ch->subs_r_tail=sub;
	}else{
		p_ch->subs_r_head=sub;
		p_ch->subs_r_tail=sub;
	}
	
	
}

struct sub_request* create_recovery_sub_write_2(struct ssd_info *ssd, int channel, int chip, int die, int plane, int block, int page){
	unsigned int mask=0; 
	struct sub_request* sub = get_sub_request_for_recovery_2(ssd, channel, chip, die, plane, block, page);
	
	if(ssd->parameter->subpage_page == 32){
		mask = 0xffffffff;
	}else{
		mask=FULL_PAGE_STATE(ssd->parameter->subpage_page);
	}
	sub->operation = WRITE;
	sub->current_state=SR_WAIT;
	sub->current_time=ssd->current_time;
	sub->begin_time = ssd->current_time;

	struct channel_info *p_ch = &ssd->channel_head[channel];
	if (p_ch->subs_w_tail!=NULL){
		p_ch->subs_w_tail->next_node=sub;
		p_ch->subs_w_tail=sub;
	}else{
		p_ch->subs_w_head=sub;
		p_ch->subs_w_tail=sub;
	}
	return sub;
}

void create_recovery_sub(struct ssd_info *ssd, int channel){
	for(int chipIndex = 0; chipIndex < ssd->parameter->chip_channel[0]; ++chipIndex){
		for(int dieIndex = 0; dieIndex < ssd->parameter->die_chip; ++dieIndex){
			for(int planeIndex = 0; planeIndex < ssd->parameter->plane_die; ++planeIndex){
				for(int blockIndex = 0; blockIndex < ssd->parameter->block_plane; ++blockIndex){
					for(int pageIndex = 0; pageIndex < ssd->parameter->page_block; ++pageIndex){
						long long raid = ssd->channel_head[channel].chip_head[chipIndex].die_head[dieIndex].plane_head[planeIndex].blk_head[blockIndex].page_head[pageIndex].raidID;
						if(raid != -1 && ssd->trip2Page[raid].using){
							int valid = ssd->channel_head[channel].chip_head[chipIndex].die_head[dieIndex].plane_head[planeIndex].blk_head[blockIndex].page_head[pageIndex].valid_state;
							ssd->needRecoveryAll++;
							if(valid == 0){
								ssd->needRecoveryInvalid++;
							}
							// for(int i = 0; i < 3;i ++){
							// 	int ppn = 0;
							// 	if(ssd->trip2Page[raid].check[i] == VAIL_DRAID){
							// 		ppn = ssd->dram->map->map_entry[ssd->trip2Page[raid].lpn[i]].pn;
							// 	}else{
							// 		abort();
							// 	}
							// 	struct local *location = find_location(ssd, ppn);
							// 	alloc_assert(location, "create_recovery_sub");
							// 	if(location->channel != channel){
							// 		create_recovery_sub_read_2(ssd, location->channel, location->chip, location->die, location->plane, location->block, location->page);
							// 	}else{
							// 		create_recovery_sub_write_2(ssd, location->channel, location->chip, location->die, location->plane, location->block, location->page);
							// 	}
							// 	free(location);
							// }
							// struct local *location = ssd->trip2Page[raid].location;
							// if(location->channel != channel){
							// 	create_recovery_sub_read_2(ssd, location->channel, location->chip, location->die, location->plane, location->block, location->page);
							// }else{
							// 	create_recovery_sub_write_2(ssd, location->channel, location->chip, location->die, location->plane, location->block, location->page);
							// }						
						}
					}
				}
			}
		}
	}
}

struct ssd_info *simulate_for_recovery(struct ssd_info *ssd)
{
    raid4_recover(ssd, 0);
    return ssd;
}

int main(int argc, char *argv[])
{
    struct ssd_info *ssd;
    char *end;
    long rounds, member = -1;
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: %s ROUNDS TRACE [RECOVER_MEMBER_0_TO_3]\n", argv[0]);
        return EXIT_FAILURE;
    }
    rounds = strtol(argv[1], &end, 10);
    if (*end || rounds < 1 || rounds > 100000 || strlen(argv[2]) >= sizeof(ssd->tracefilename)) {
        fprintf(stderr, "Invalid round count or trace filename (maximum 1023 bytes).\n");
        return EXIT_FAILURE;
    }
    if (argc == 4) {
        member = strtol(argv[3], &end, 10);
        if (*end || member < 0 || member > 3) {
            fprintf(stderr, "Recovery member must be 0, 1, 2, or 3.\n");
            return EXIT_FAILURE;
        }
    }
    ssd = calloc(1, sizeof(*ssd));
    alloc_assert(ssd, "ssd");
    strcpy(ssd->tracefilename, argv[2]);
    if (!initiation(ssd)) return EXIT_FAILURE;
    if (ssd->parameter->channel_number != 16 || ssd->parameter->chip_num != 32 ||
        ssd->parameter->allocation_scheme != 0 || ssd->parameter->dynamic_allocation != 2 ||
        ssd->parameter->advanced_commands != 0 || ssd->parameter->subpage_page < 1 ||
        ssd->parameter->subpage_page > 32) {
        fprintf(stderr, "RAID4 requires 16 channels, 32 chips, allocation=0, dynamic_allocation=2, advanced command=0, and 1..32 sectors/page.\n");
        return EXIT_FAILURE;
    }
    for (unsigned ch = 0; ch < 16; ++ch) {
        if (ssd->parameter->chip_channel[ch] != 2) {
            fprintf(stderr, "RAID4 requires two chips on every channel.\n");
            return EXIT_FAILURE;
        }
    }
    raid_init(ssd);
    raid4_run(ssd, (int)rounds, (int)member);
    raid4_destroy(ssd);
    free_all_node(ssd);
    return EXIT_SUCCESS;
}

/******************simulate() *********************************************************************
*simulate()�Ǻ��Ĵ�����������Ҫʵ�ֵĹ��ܰ���
*1,��trace�ļ��л�ȡһ�����󣬹ҵ�ssd->request
*2������ssd�Ƿ���dram�ֱ��������������󣬰���Щ��������Ϊ��д�����󣬹ҵ�ssd->channel����ssd��
*3�������¼����Ⱥ���������Щ��д������
*4�����ÿ������������󶼴������������Ϣ��outputfile�ļ���
**************************************************************************************************/
struct ssd_info *simulate(struct ssd_info *ssd)
{
	int flag=1,flag1=0;
	double output_step=0;
	unsigned int a=0,b=0;
	//errno_t err;

	printf("\n");
	printf("begin simulating.......................\n");
	printf("\n");
	printf("\n");
	printf("   ^o^    OK, please wait a moment, and enjoy music and coffee   ^o^    \n");

	ssd->tracefile = fopen(ssd->tracefilename,"r");
	if(ssd->tracefile == NULL)
	{  
		printf("the trace file can't open\n");
		return NULL;
	}

	fprintf(ssd->outputfile,"      arrive           lsn     size ope     begin time    response time    process time\n");	
	fflush(ssd->outputfile);

	while(flag!=100)      
	{
        
		flag=get_requests(ssd);
		if (flag == 0) {
			flag = 100;
			printf("end pre\n");
		}	
		if(flag == 1)
		{   
			//printf("once\n");
			if (ssd->parameter->dram_capacity!=0)
			{
				buffer_management(ssd);  
				//distribute(ssd); 
			} 
			else
			{
				no_buffer_distribute(ssd);
			}		
		}

		//if(ssd->cpu_current_state == CPU_IDLE || (ssd->cpu_next_state == CPU_IDLE && ssd->cpu_next_state_predict_time >= ssd->current_time)){
			ssd->cpu_current_state = CPU_IDLE;
			process(ssd);
		//}
			
		
		trace_output(ssd);
		if (flag == 0) {
			flag = 100;
			printf("end last\n");
		}
			
	}

	fclose(ssd->tracefile);
	return ssd;
}

struct ssd_info *simulate_multiple(struct ssd_info *ssd, int sTIMES)
{
	int flag=1,flag1=0;
	double output_step=0;
	unsigned int a=0,b=0;
	//errno_t err;

	int simulate_times = 0;
	//int sTIMES = 10;
	printf("\n");
	printf("begin simulating.......................\n");
	printf("\n");
	printf("\n");
	printf("   ^o^    OK, please wait a moment, and enjoy music and coffee   ^o^    \n");

	/************************IMPORTANT FACTOR************************************/
	//ssd->parameter->turbo_mode_factor = 100;
	fprintf(ssd->statisticfile, "requests            time       gc_count          r_lat          w_lat          w_lsb          w_csb          w_msb           f_gc       mov_page      free_lsb     r_req_count    w_req_count    same_pages       req_lsb       req_csb       req_msb       w_req_count_l    same_pages_l       req_lsb_l       req_csb_l       req_msb_l\n");
	
	while(simulate_times < sTIMES){
		ssd->tracefile = fopen(ssd->tracefilename,"r");
		if(ssd->tracefile == NULL){  
			printf("the trace file can't open\n");
			return NULL;
		}
		fseek(ssd->tracefile,0,SEEK_SET);
		while(flag!=100)      
		{
			flag=get_requests(ssd);
			if (flag == 0) {
				flag = 100;
				printf("end pre\n");
			}	
			if(flag == 1)
			{   
				//printf("once\n");
				if (0 && ssd->parameter->dram_capacity!=0)
				{
					buffer_management(ssd);  
					//distribute(ssd); 
				} 
				else
				{
					no_buffer_distribute(ssd);
				}		
			}

			//if(ssd->cpu_current_state == CPU_IDLE || (ssd->cpu_next_state == CPU_IDLE && ssd->cpu_next_state_predict_time >= ssd->current_time)){
				if (ssd->cpu_next_state_predict_time <= ssd->current_time) ssd->cpu_current_state = CPU_IDLE;
				process(ssd);
			//}
			
		
			trace_output(ssd);
			if (flag == 0) {
				flag = 100;
				printf("end last\n");
			}
				
		}
		ssd->basic_time = ssd->current_time;
		printf("%lld %lld\n", ssd->basic_time, ssd->current_time);
		simulate_times++;
		flag = 1;
		printf("%d %d\n", simulate_times, sTIMES);
		fclose(ssd->tracefile);
	}
	
	return ssd;
}


unsigned int XOR_process(struct ssd_info *ssd, int sectors)
{
    uint64_t duration = (19000ULL * (unsigned)sectors + ssd->parameter->subpage_page - 1) / ssd->parameter->subpage_page;
    int64_t start = ssd->current_time;
    if (ssd->cpu_next_state_predict_time > start) start = ssd->cpu_next_state_predict_time;
    ssd->cpu_current_state = CPU_BUSY;
    ssd->cpu_next_state = CPU_IDLE;
    ssd->cpu_next_state_predict_time = start + duration;
    return (unsigned)duration;
}


/********    get_request    ******************************************************
*	1.get requests that arrived already
*	2.add those request node to ssd->reuqest_queue
*	return	0: reach the end of the trace
*			-1: no request has been added
*			1: add one request to list
*SSDģ����������������ʽ:ʱ������(��ȷ��̫��) �¼�����(���������) trace����()��
*���ַ�ʽ�ƽ��¼���channel/chip״̬�ı䡢trace�ļ�����ﵽ��
*channel/chip״̬�ı��trace�ļ����󵽴���ɢ����ʱ�����ϵĵ㣬ÿ�δӵ�ǰ״̬����
*��һ��״̬��Ҫ���������һ��״̬��ÿ����һ����ִ��һ��process
********************************************************************************/
int get_requests(struct ssd_info *ssd)  
{  
	char buffer[200];
	unsigned int lsn=0,large_lsn;
	int device,  size, ope, i = 0,j=0;
	int priority;
	struct request *request1;
	int flag = 1;
	long filepoint; 
	int64_t time_t = 0;
	int64_t nearest_event_time;    

	#ifdef DEBUG
	printf("enter get_requests,  current time:%lld\n",ssd->current_time);
	#endif

	if(feof(ssd->tracefile)) {
		/*if(ssd->gc_request > 10) {
			printf("ssd->gc_request is %d\n", ssd->gc_request);
			ssd->current_time += 100;
			return -1;
		}*/
		return 0;
	}
		

	filepoint = ftell(ssd->tracefile);	
	fgets(buffer, 200, ssd->tracefile); 
	if(strlen(buffer) <= 1){
		return 0;
	}
	sscanf(buffer,"%lld %d %d %d %d %d",&time_t,&device,&lsn,&size,&ope,&priority);
	
	//printf("%lld\n", time_t);
    //=======================================
    priority = 1;   //ǿ���趨���ȼ�
   time_t = time_t/(ssd->speed_up);
	if(ssd->firstTime_ == 0){
		ssd->firstTime_ = time_t;
	}
	if(time_t > 0){
		time_t -= ssd->firstTime_;
		time_t = ssd->basic_time + time_t;
	}else{
		time_t = ssd->current_time;
	}
	//=======================================
	if ((device<0)&&(lsn<0)&&(size<0)&&(ope<0))
	{
		return 100;
	}
	if(size==0){
		size = 1;
		}
	if (lsn<ssd->min_lsn) 
		ssd->min_lsn=lsn;
	if (lsn>ssd->max_lsn)
		ssd->max_lsn=lsn;
	/******************************************************************************************************
	*�ϲ��ļ�ϵͳ���͸�SSD���κζ�д��������������֣�LSN��size�� LSN���߼������ţ������ļ�ϵͳ���ԣ����������Ĵ�
	*���ռ���һ�����Ե������ռ䡣���磬������260��6����ʾ������Ҫ��ȡ��������Ϊ260���߼�������ʼ���ܹ�6��������
	*large_lsn: channel�����ж��ٸ�subpage�������ٸ�sector��overprovideϵ����SSD�в��������еĿռ䶼���Ը��û�ʹ�ã�
	*����32G��SSD������10%�Ŀռ䱣�������������ã����Գ���1-provide
	***********************************************************************************************************/
	if(ssd->parameter->allocation_scheme==0 && ssd->parameter->dynamic_allocation == 2)
		large_lsn=(int)((ssd->parameter->subpage_page*ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*(ssd->parameter->chip_num))*(1-ssd->parameter->overprovide)*0.75);
	else
		large_lsn=(int)((ssd->parameter->subpage_page*ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num)*(1-ssd->parameter->overprovide));
	large_lsn = ssd->stripe.checkStart - 1;
	large_lsn *= ssd->parameter->subpage_page;
	lsn = lsn%large_lsn;

	nearest_event_time=find_nearest_event(ssd);
	if (nearest_event_time==MAX_INT64)
	{
		if(time_t > ssd->current_time)
			ssd->current_time=time_t;            
		                                                  
		//if (ssd->request_queue_length>ssd->parameter->queue_length)    //���������еĳ��ȳ����������ļ��������õĳ���                     
		//{
			//printf("error in get request , the queue length is too long\n");
		//}
	}
	else
	{   
		if(nearest_event_time<time_t)
		{
			/*******************************************************************************
			*�ع��������û�а�time_t����ssd->current_time����trace�ļ��Ѷ���һ����¼�ع�
			*filepoint��¼��ִ��fgets֮ǰ���ļ�ָ��λ�ã��ع����ļ�ͷ+filepoint��
			*int fseek(FILE *stream, long offset, int fromwhere);���������ļ�ָ��stream��λ�á�
			*���ִ�гɹ���stream��ָ����fromwhere��ƫ����ʼλ�ã��ļ�ͷ0����ǰλ��1���ļ�β2��Ϊ��׼��
			*ƫ��offset��ָ��ƫ���������ֽڵ�λ�á����ִ��ʧ��(����offset�����ļ�������С)���򲻸ı�streamָ���λ�á�
			*�ı��ļ�ֻ�ܲ����ļ�ͷ0�Ķ�λ��ʽ���������д��ļ���ʽ��"r":��ֻ����ʽ���ı��ļ�	
			**********************************************************************************/
			fseek(ssd->tracefile,filepoint,0); 
			if(ssd->current_time<=nearest_event_time)
				ssd->current_time=nearest_event_time;
			return -1;
		}
		else
		{
			if (ssd->request_queue_length>=ssd->parameter->queue_length)
			{
				fseek(ssd->tracefile,filepoint,0);
				ssd->current_time=nearest_event_time;
				return -1;
			} 
			else
			{
				ssd->current_time=time_t;
			}
		}
	}

	if(time_t < 0)
	{
		printf("error!\n");
		while(1){}
	}

	if(feof(ssd->tracefile))
	{
		/*if(ssd->gc_request > 10){ main
			printf("ssd->gc_request is %d\n", ssd->gc_request);
			ssd->current_time += 100;
			return -1;
		}*/
		return 0;
	}

	request1 = (struct request*)malloc(sizeof(struct request));
	alloc_assert(request1,"request");
	memset(request1,0, sizeof(struct request));

	request1->completeFlag = 0;
	request1->all = 0;
	request1->now = 0;
	request1->time = time_t;
	request1->lsn = lsn;
	request1->size = size;
	request1->operation = ope;	
	request1->priority = priority;
	request1->begin_time = time_t;
	request1->response_time = 0;	
	request1->energy_consumption = 0;	
	request1->next_node = NULL;
	request1->distri_flag = 0;              // indicate whether this request has been distributed already
	request1->subs = NULL;
	request1->need_distr_flag = NULL;
	request1->complete_lsn_count=0;         //record the count of lsn served by buffer
	request1->MergeFlag = 0;
	filepoint = ftell(ssd->tracefile);		// set the file point
	ssd->counttmp1++;

	ssd->preRequestArriveTime[ssd->preRequestArriveTimeIndex] = time_t;
	ssd->preRequestArriveTimeIndex = (ssd->preRequestArriveTimeIndex + 1) % ssd->preRequestArriveTimeIndexMAX;
	//request1->operation = WRITE;
	
	if(request1->operation == 1 && lsn == 3686940 ) {
		printf("read!!!!\n");
	}
	
	if(ssd->request_queue == NULL)          //The queue is empty
	{
		ssd->request_queue = request1;
		ssd->request_tail = request1;
		ssd->request_queue_length++;
	}
	else
	{			
		(ssd->request_tail)->next_node = request1;	
		ssd->request_tail = request1;			
		ssd->request_queue_length++;
	}
	if(ssd->request_queue_length > ssd->max_queue_depth){
		ssd->max_queue_depth = ssd->request_queue_length;
		}
	/*
	if(ssd->request_queue_length > 10){
		if(ssd->parameter->turbo_mode==1 && ssd->performance_mode==0){
			ssd->parameter->turbo_mode_factor = 100;
			ssd->parameter->turbo_mode_factor_2 = 100;
			//printf("Ajusting to high performance, %d, %d.\n", ssd->parameter->turbo_mode_factor, ssd->parameter->turbo_mode_factor_2);
			ssd->performance_mode = 1;
			}
		}
	else if(ssd->request_queue_length < 5){
		if(ssd->parameter->turbo_mode==1 && ssd->performance_mode==1){
			//float turbo_mode_factor, turbo_mode_factor_2;
			//turbo_mode_factor = (((float)ssd->free_lsb_count)/(ssd->free_lsb_count+ssd->free_csb_count+ssd->free_msb_count))*100;
			//turbo_mode_factor_2 = (((float)(ssd->free_lsb_count+ssd->free_csb_count))/(ssd->free_lsb_count+ssd->free_csb_count+ssd->free_msb_count))*100;
			ssd->parameter->turbo_mode_factor = 34;
			ssd->parameter->turbo_mode_factor_2 = 67;
			ssd->performance_mode = 0;
			}
		}
	*/
	if (request1->operation==1)             //����ƽ�������С 1Ϊ�� 0Ϊд
	{
		ssd->ave_read_size=(ssd->ave_read_size*ssd->read_request_count+request1->size)/(ssd->read_request_count+1);
	} 
	else
	{
		ssd->ave_write_size=(ssd->ave_write_size*ssd->write_request_count+request1->size)/(ssd->write_request_count+1);
	}

	
	filepoint = ftell(ssd->tracefile);	
	fgets(buffer, 200, ssd->tracefile);    //Ѱ����һ������ĵ���ʱ��
	if(feof(ssd->tracefile)){
		return 0;
	}
	sscanf(buffer,"%lld %d %d %d %d",&time_t,&device,&lsn,&size,&ope);
	ssd->next_request_time=time_t;
	fseek(ssd->tracefile,filepoint,0);
	
	return 1;
}

/*unsigned char read_buffer(struct ssd_info *ssd, struct request *req){
	unsigned int lsn,lpn,last_lpn,first_lpn,state,full_page, mask;
	unsigned int sub_size=0;
	unsigned int sub_state=0;
	unsigned char ret = 0;
	
	lsn=req->lsn;
	lpn=req->lsn/ssd->parameter->subpage_page;
	last_lpn=(req->lsn+req->size-1)/ssd->parameter->subpage_page;
	first_lpn=req->lsn/ssd->parameter->subpage_page;
	struct sub_request *sub=NULL;
	if(ssd->parameter->subpage_page == 32){
		mask = 0xffffffff;
	}else{
		mask=FULL_PAGE_STATE(ssd->parameter->subpage_page);
	}
	if(req->operation==READ)        
	{	
		
		if((lpn * ssd->parameter->subpage_page + ssd->parameter->subpage_page) > (req->lsn+req->size)){
			sub = creat_sub_request(ssd,lpn,size(mask),mask,req,req->operation,0);
			sub->readXorFlag = 1;
			ret = 1;
		}	
		if(lpn != last_lpn){
			if((last_lpn * ssd->parameter->subpage_page + ssd->parameter->subpage_page) > (req->lsn+req->size)){
				sub = creat_sub_request(ssd,last_lpn,size(mask),mask,req,req->operation,0);
				sub->readXorFlag = 1;
				ret = 1;
			}
		}
		

		
	}
	return ret;
}*/


/**********************************************************************************************************************************************
*����buffer�Ǹ�дbuffer������Ϊд�������ģ���Ϊ��flash��ʱ��tRΪ20us��дflash��ʱ��tprogΪ200us������Ϊд������ܽ�ʡʱ��
*  �����������������buffer����buffer������ռ��channel��I/O���ߣ�û������buffer����flash����ռ��channel��I/O���ߣ����ǲ���buffer��
*  д����������request�ֳ�sub_request����������Ƕ�̬���䣬sub_request�ҵ�ssd->sub_request�ϣ���Ϊ��֪��Ҫ�ȹҵ��ĸ�channel��sub_request��
*          ����Ǿ�̬������sub_request�ҵ�channel��sub_request����,ͬʱ���ܶ�̬���仹�Ǿ�̬����sub_request��Ҫ�ҵ�request��sub_request����
*		   ��Ϊÿ������һ��request����Ҫ��traceoutput�ļ�������������request����Ϣ��������һ��sub_request,�ͽ����channel��sub_request��
*		   ��ssd��sub_request����ժ����������traceoutput�ļ����һ���������request��sub_request����
*		   sub_request����buffer����buffer����д�����ˣ����ҽ���sub_page�ᵽbuffer��ͷ(LRU)����û��������buffer�������Ƚ�buffer��β��sub_request
*		   д��flash(������һ��sub_requestд���󣬹ҵ��������request��sub_request���ϣ�ͬʱ�Ӷ�̬���仹�Ǿ�̬����ҵ�channel��ssd��
*		   sub_request����),�ڽ�Ҫд��sub_pageд��buffer��ͷ
***********************************************************************************************************************************************/
struct ssd_info *buffer_management(struct ssd_info *ssd)
{   
	unsigned int j,lsn,lpn,last_lpn,first_lpn,index,complete_flag=0, state,full_page;
	unsigned int flag=0,need_distb_flag,lsn_flag,flag1=1,active_region_flag=0;           
	struct request *new_request;
	struct buffer_group *buffer_node,key;
	unsigned int mask=0,offset1=0,offset2=0;

	#ifdef DEBUG
	printf("enter buffer_management,  current time:%lld\n",ssd->current_time);
	#endif
	ssd->dram->current_time=ssd->current_time;
	full_page=~(0xffffffff<<ssd->parameter->subpage_page);
	
	new_request=ssd->request_tail;
	lsn=new_request->lsn;
	lpn=new_request->lsn/ssd->parameter->subpage_page;
	last_lpn=(new_request->lsn+new_request->size-1)/ssd->parameter->subpage_page;
	first_lpn=new_request->lsn/ssd->parameter->subpage_page;

	new_request->need_distr_flag=(unsigned int*)malloc(sizeof(unsigned int)*((last_lpn-first_lpn+1)*ssd->parameter->subpage_page/32+1));
	alloc_assert(new_request->need_distr_flag,"new_request->need_distr_flag");
	memset(new_request->need_distr_flag, 0, sizeof(unsigned int)*((last_lpn-first_lpn+1)*ssd->parameter->subpage_page/32+1));
	
	if(new_request->operation==READ) 
	{		
		while(lpn<=last_lpn)      		
		{
			/************************************************************************************************
			 *need_distb_flag��ʾ�Ƿ���Ҫִ��distribution������1��ʾ��Ҫִ�У�buffer��û�У�0��ʾ����Ҫִ��
             *��1��ʾ��Ҫ�ַ���0��ʾ����Ҫ�ַ�����Ӧ���ʼȫ����Ϊ1
			*************************************************************************************************/
			unsigned char readBufHit = 0;
			unsigned int readBitMAp = 0;
			unsigned int readHitMAp = 0;
			need_distb_flag=full_page;   
			key.group=lpn;
			//buffer_node= (struct buffer_group*)avlTreeFind(ssd->dram->buffer, (TREE_NODE *)&key);		// buffer node 
			
			buffer_node= (struct buffer_group*)hash_find(ssd->dram->buffer, (HASH_NODE *)&key);
			while((buffer_node!=NULL)&&(lsn<(lpn+1)*ssd->parameter->subpage_page)&&(lsn<=(new_request->lsn+new_request->size-1)))             			
			{      
				
				lsn_flag=full_page;
				mask=1 << (lsn%ssd->parameter->subpage_page);
				readBitMAp |= mask;
				if(mask> (1 <<31))
				{
					printf("the subpage number is larger than 32!add some cases");
					getchar(); 		   
				}
				else if((buffer_node->stored & mask)==mask)
				{
					flag=1;
					lsn_flag=lsn_flag&(~mask);
				}

				if(flag==1)				
				{	//�����buffer�ڵ㲻��buffer�Ķ��ף���Ҫ������ڵ��ᵽ���ף�ʵ����LRU�㷨�������һ��˫����С�		       		
					if(ssd->dram->buffer->buffer_head!=buffer_node)     
					{		
						if(ssd->dram->buffer->buffer_tail==buffer_node)								
						{			
							buffer_node->LRU_link_pre->LRU_link_next=NULL;					
							ssd->dram->buffer->buffer_tail=buffer_node->LRU_link_pre;							
						}				
						else								
						{				
							buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;				
							buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;								
						}								
						buffer_node->LRU_link_next=ssd->dram->buffer->buffer_head;
						ssd->dram->buffer->buffer_head->LRU_link_pre=buffer_node;
						buffer_node->LRU_link_pre=NULL;			
						ssd->dram->buffer->buffer_head=buffer_node;													
					}
					
					
					readHitMAp |= mask;
					readBufHit = 1;
					
					
									
					new_request->complete_lsn_count++;											
				}		
				else if(flag==0){
				}
				need_distb_flag=need_distb_flag&lsn_flag;
				
				flag=0;		
				lsn++;
		
			}	
			while((buffer_node == NULL)&&(lsn<(lpn+1)*ssd->parameter->subpage_page)&&(lsn<=(new_request->lsn+new_request->size-1))){
				mask=1 << (lsn%ssd->parameter->subpage_page);
				readBitMAp |= mask;
				lsn++;
				
			}
			if(need_distb_flag){
				new_request->all++;
				new_request->now++;
				ssd->dram->buffer->read_miss_hit++;
				struct sub_request *sub = creat_sub_request(ssd,lpn,size(need_distb_flag), need_distb_flag,new_request,new_request->operation,0, ssd->page2Trip[lpn]);
			}else{
				ssd->dram->buffer->read_hit++;
			}
			
			
			if(readBufHit == 1){	
				
				readBufHit = 0;
			}else{
				
			}
			
			index=(lpn-first_lpn)/(32/ssd->parameter->subpage_page); 			
			new_request->need_distr_flag[index]=new_request->need_distr_flag[index]|(need_distb_flag<<(((lpn-first_lpn)%(32/ssd->parameter->subpage_page))*ssd->parameter->subpage_page));	
			lpn++;
			
		}
	}  
	else if(new_request->operation==WRITE)
	{
		while(lpn<=last_lpn)           	
		{	
			need_distb_flag=full_page;
			mask=FULL_PAGE_STATE(ssd->parameter->subpage_page);
			state=mask;

			if(lpn==first_lpn)
			{
				offset1=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-new_request->lsn);
				state=state&(0xffffffff<<offset1);
			}
			if(lpn==last_lpn)
			{
				offset2=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-(new_request->lsn+new_request->size));
				state=state&(~(0xffffffff<<offset2));
			}
			
			ssd=insert2buffer(ssd, lpn, state,NULL,new_request);
			lpn++;
		}
		
	}
	complete_flag = 1;
	for(j=0;j<=(last_lpn-first_lpn+1)*ssd->parameter->subpage_page/32;j++)
	{
		if(new_request->need_distr_flag[j] != 0)
		{
			complete_flag = 0;
		}
	}
	/*************************************************************
	*��������Ѿ���ȫ����buffer���񣬸�������Ա�ֱ����Ӧ��������
	*�������dram�ķ���ʱ��Ϊ1000ns
	**************************************************************/
	if((complete_flag == 1) && (new_request->subs == NULL) && (new_request->all == 0))               
	{
		if(new_request->operation==WRITE){
			new_request->begin_time=ssd->current_time;
			new_request->response_time=ssd->current_time+1000;
		}/*else if(read_buffer(ssd, new_request) == 0){
			new_request->begin_time=ssd->current_time;
			new_request->response_time=ssd->current_time+1000;
		}*/           
	}

	return ssd;
}

/*****************************
*lpn��ppn��ת����
******************************/
unsigned int lpn2ppn(struct ssd_info *ssd,unsigned int lsn)
{
	int lpn, ppn;	
	struct entry *p_map = ssd->dram->map->map_entry;                //��ȡӳ���
#ifdef DEBUG
	printf("enter lpn2ppn,  current time:%lld\n",ssd->current_time);
#endif
	lpn = lsn/ssd->parameter->subpage_page;			//subpage_pageָһ��page�а�������ҳ�������ڲ����ļ��п����趨
	ppn = (p_map[lpn]).pn;                     //�߼�ҳlpn������ҳppn��ӳ���¼��ӳ�����
	return ppn;
}

/**********************************************************************************
*�����������������������ֻ����������д�����Ѿ���buffer_management()�����д�����
*����������к�buffer���еļ�飬��ÿ������ֽ�������󣬽���������й���channel�ϣ�
*��ͬ��channel���Լ������������
**********************************************************************************/

struct ssd_info *distribute(struct ssd_info *ssd) 
{
	unsigned int start, end, first_lsn,last_lsn,lpn,flag=0,flag_attached=0,full_page;
	unsigned int j, k, sub_size;
	int i=0;
	struct request *req;
	struct sub_request *sub;
	unsigned int* complt;
	return ssd;
	#ifdef DEBUG
	printf("enter distribute,  current time:%lld\n",ssd->current_time);
	#endif
	full_page=~(0xffffffff<<ssd->parameter->subpage_page);

	req = ssd->request_tail;
	if(req->response_time != 0){
		return ssd;
	}
	if (req->operation==WRITE)
	{
		return ssd;
	}

	if(req != NULL)
	{
		
		if(req->distri_flag == 0)
		{
			//�������һЩ��������Ҫ����
			if(req->complete_lsn_count != ssd->request_tail->size)
			{		
				first_lsn = req->lsn;				
				last_lsn = first_lsn + req->size;
				complt = req->need_distr_flag;
				start = first_lsn - first_lsn % ssd->parameter->subpage_page;
				end = (last_lsn/ssd->parameter->subpage_page + 1) * ssd->parameter->subpage_page;
				i = (end - start - 1)/32;	// ?

				while(i >= 0)
				{	
					/*************************************************************************************
					*һ��32λ���������ݵ�ÿһλ����һ����ҳ��32/ssd->parameter->subpage_page�ͱ�ʾ�ж���ҳ��
					*�����ÿһҳ��״̬��������� req->need_distr_flag�У�Ҳ����complt�У�ͨ���Ƚ�complt��
					*ÿһ����full_page���Ϳ���֪������һҳ�Ƿ�����ɡ����û���������ͨ��creat_sub_request
					��������������
					*************************************************************************************/
					
					for(j=0; j<32/ssd->parameter->subpage_page; j++)
					{	
						k = (complt[((end-start - 1)/32-i)] >>(ssd->parameter->subpage_page*j)) & full_page;	
						ssd->read2++;
						if (k !=0)
						{
							
							lpn = start/ssd->parameter->subpage_page+ ((end-start - 1)/32-i)*32/ssd->parameter->subpage_page + j;
							sub_size=transfer_size(ssd,k,lpn,req);
							
							if (sub_size==0) 
							{
								continue;
							}
							else
							{	
								ssd->read4++;
								sub=creat_sub_request(ssd,lpn,sub_size,0,req,req->operation,0, ssd->page2Trip[lpn]);
								if(sub_size != ssd->parameter->subpage_page)
									sub->readXorFlag = 1;
							}	
						}
					}
					i = i-1;
				}

			}
			else if(req->subs == NULL)
			{
				req->begin_time=ssd->current_time;
				req->response_time=ssd->current_time+1000;   
			}

		}
	}
	return ssd;
}

void delete_req(struct ssd_info *ssd ,struct request **pre_node, struct request **req){
	struct sub_request *tmp;
	
	while((*req)->subs!=NULL)
	{	
		tmp = (*req)->subs;
		unsigned long long READTIME = ssd->parameter->time_characteristics.tR + 7*ssd->parameter->time_characteristics.tWC + (tmp->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;
		if(tmp->operation == READ ){
			ssd->chanSubRWriteTime[tmp->location->channel] += ((tmp->complete_time -  (*req)->time) - READTIME);
			++ssd->subRWaitCount;
		}else{
			unsigned long long WRITETIME =7*ssd->parameter->time_characteristics.tWC+(tmp->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC + ssd->parameter->time_characteristics.tPROG;
			ssd->chanSubRWriteTime_2 += ((tmp->complete_time -  (*req)->time) - WRITETIME);
			++ssd->subRWaitCount_2;
		}
		
		(*req)->subs = tmp->next_subs;
		if (tmp->update!=NULL)
		{
			unsigned long long READTIME = ssd->parameter->time_characteristics.tR + 7*ssd->parameter->time_characteristics.tWC + (tmp->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;
			//if(((tmp->update->complete_time - tmp->update->begin_time)) != 1000){
				ssd->chanSubRWriteTime[tmp->update->location->channel] += ((tmp->update->complete_time - (*req)->time) - READTIME);
				++ssd->subRWaitCount;
			//}
			free(tmp->update->location);
			tmp->update->location=NULL;
			free(tmp->update);
			tmp->update=NULL;
		}
		free(tmp->location);
		tmp->location=NULL;
		free(tmp);
		tmp=NULL;
	}
				
	if( (*pre_node) == NULL)
	{
		if((*req)->next_node == NULL)
		{
			free((*req)->need_distr_flag);
			(*req)->need_distr_flag=NULL;
			free((*req));
			(*req) = NULL;
			ssd->request_queue = NULL;
			ssd->request_tail = NULL;
			ssd->request_queue_length--;
		}
		else
		{
			ssd->request_queue = (*req)->next_node;
			(*pre_node) = (*req);
			(*req) = (*req)->next_node;
			free((*pre_node)->need_distr_flag);
			(*pre_node)->need_distr_flag=NULL;
			free((*pre_node));
			(*pre_node) = NULL;
			ssd->request_queue_length--;
		}
	}
	else
	{
		if((*req)->next_node == NULL)
		{
			(*pre_node)->next_node = NULL;
			free((*req)->need_distr_flag);
			(*req)->need_distr_flag=NULL;
			free((*req));
			(*req) = NULL;
			ssd->request_tail = (*pre_node);	
			ssd->request_queue_length--;
		}
		else
		{
			(*pre_node)->next_node = (*req)->next_node;
			free((*req)->need_distr_flag);
			(*req)->need_distr_flag=NULL;
			free((*req));
			(*req) = (*pre_node)->next_node;
			ssd->request_queue_length--;
		}

	}
}

/**********************************************************************
*trace_output()��������ÿһ����������������󾭹�process()�����������
*��ӡ�����ص����н����outputfile�ļ��У�����Ľ����Ҫ�����е�ʱ��
**********************************************************************/
void trace_output(struct ssd_info* ssd){
	int flag = 1;	
	int64_t start_time, end_time;
	struct request *req, *pre_node;
	struct sub_request *sub, *tmp;

	unsigned int full_page;

#ifdef DEBUG
	printf("enter trace_output,  current time:%lld\n",ssd->current_time);
#endif
	int debug_0918 = 0;
	pre_node=NULL;
	req = ssd->request_queue;
	start_time = 0;
	end_time = 0;

	if(req == NULL)
		return;
	if(ssd->parameter->subpage_page == 32){
		full_page = 0xffffffff;
	}
	else{
		full_page=FULL_PAGE_STATE(ssd->parameter->subpage_page);
	}
	req = ssd->raidReq;
	sub = req->subs;
	struct sub_request *preCacheNode = NULL;
	while(sub){
		//printf("%p %p\n", sub, ssd->raidReq->subs);
		if((sub->current_state == SR_COMPLETE)||((sub->next_state==SR_COMPLETE)&&(sub->next_state_predict_time<=ssd->current_time))){
			//printf("free \n");
			if(sub->operation == READ && sub->readRaidLpn != 0){
				signed int mask=FULL_PAGE_STATE(ssd->parameter->subpage_page);
				struct sub_request *writeSub =  creat_sub_request(ssd,  sub->readRaidLpn, size(full_page), full_page,\
								ssd->raidReq, WRITE, TARGET_LSB, ssd->page2Trip[sub->lpn]);
				 
				writeSub->readXorFlag = 1;
				XOR_process(ssd, 16);
			}
			if(!preCacheNode){
				req->subs = sub->next_subs;
				free(sub->location);
				free(sub);
				sub = req->subs;
			}else {
				preCacheNode->next_subs = sub->next_subs;
				free(sub->location);
				free(sub);
				sub = preCacheNode->next_subs;
			}
		}else{	
			preCacheNode = sub;
			sub = sub->next_subs;
		}
	}

	req = ssd->readBufReqQue;
	sub = req->subs;
	preCacheNode = NULL;
	while(sub){
		//printf("%p %p\n", sub, ssd->raidReq->subs);
		if((sub->current_state == SR_COMPLETE)||((sub->next_state==SR_COMPLETE)&&(sub->next_state_predict_time<=ssd->current_time))){
			//printf("free \n");
			if(!preCacheNode){
				req->subs = sub->next_subs;
				free(sub->location);
				free(sub);
				sub = req->subs;
			}else {
				preCacheNode->next_subs = sub->next_subs;
				free(sub->location);
				free(sub);
				sub = preCacheNode->next_subs;
			}
		}else{	
			preCacheNode = sub;
			sub = sub->next_subs;
		}
	}
	
	req = ssd->request_queue;
	sub = req->subs;
	while(req != NULL)	
	{
		sub = req->subs;
		flag = 1;
		start_time = 0;
		end_time = 0;
		if(req->response_time != 0)
		{
			//printf("Rsponse time != 0?\n");
			fprintf(ssd->outputfile,"%16lld %10d %6d %2d %16lld %16lld %10lld\n",req->time,req->lsn, req->size, req->operation, req->begin_time, req->response_time, req->response_time-req->time);
			fflush(ssd->outputfile);
			ssd->completed_request_count++;
			if(ssd->completed_request_count%10000 == 0){
				printf("completed requests: %ld\n", ssd->completed_request_count);
				//statistic_output_easy(ssd, ssd->completed_request_count);
				ssd->newest_read_avg = 0;
				ssd->newest_write_avg = 0;
				ssd->newest_read_request_count = 0;
				ssd->newest_write_request_count = 0;
				ssd->newest_write_lsb_count = 0;
				ssd->newest_write_csb_count = 0;
				ssd->newest_write_msb_count = 0;
				ssd->newest_write_request_completed_with_same_type_pages = 0;
				//***************************************************************************
				int channel;
				int this_day;
				this_day = (int)(ssd->current_time/1000000000/3600/24);
				/*
				if(this_day>ssd->time_day){
					printf("Another Day begin, %d.\n", this_day);
					}
					*/
				if(this_day>ssd->time_day){
					printf("Day %d begin......\n", this_day);
					ssd->time_day = this_day;
					if((ssd->parameter->dr_switch==1)&&(this_day%ssd->parameter->dr_cycle==0)){
						for(channel=0;channel<ssd->parameter->channel_number;channel++){
							dr_for_channel(ssd, channel);
							}
						}
					/*
					if((ssd->parameter->turbo_mode==2)&&(this_day%7==3)){
						printf("Enter turbo-mode.....\n");
						ssd->parameter->lsb_first_allocation = 1;
						ssd->parameter->fast_gc = 1;
						}
					else if(ssd->parameter->turbo_mode==2){
						//printf("Exist turbo-mode....\n");
						ssd->parameter->lsb_first_allocation = 0;
						ssd->parameter->fast_gc = 0;
						}
						*/
					}
				//***************************************************************************
				}
			
			if(debug_0918){
				printf("completed requests: %ld\n", ssd->completed_request_count);
				}
			
			if(req->response_time-req->begin_time==0)
			{
				printf("the response time is 0?? \n");
				getchar();
			}

			if (req->operation==READ)
			{
				ssd->read_request_count++;
				ssd->read_avg=ssd->read_avg+(req->response_time-req->time);
				//===========================================
				ssd->newest_read_request_count++;
				ssd->newest_read_avg = ssd->newest_read_avg+(end_time-req->time);
				//===========================================
			} 
			else
			{
				ssd->write_request_count++;
				ssd->write_avg=ssd->write_avg+(req->response_time-req->time);
				//===========================================
				ssd->newest_write_request_count++;
				ssd->newest_write_avg = ssd->newest_write_avg+(end_time-req->time);
				ssd->last_write_lat = end_time-req->time;
				//--------------------------------------------
				int new_flag = 1;
				int origin;
				struct sub_request *next_sub_a;
				next_sub_a = req->subs;
				/*origin = next_sub_a->allocated_page_type;
				next_sub_a = next_sub_a->next_subs;
				while(next_sub_a!=NULL){
					if(next_sub_a->allocated_page_type != origin){
						new_flag = 0;
						break;
						}
					next_sub_a = next_sub_a->next_subs;
					}
				if(new_flag==1){
					ssd->newest_write_request_completed_with_same_type_pages++;
					if(origin==1){
						ssd->newest_msb_request_a++;
						}
					else{
						ssd->newest_lsb_request_a++;
						}
					}*/
				//-------------------------------------------

				//===========================================
			}

			if(pre_node == NULL)
			{
				if(req->next_node == NULL)
				{
					free(req->need_distr_flag);
					req->need_distr_flag=NULL;
					free(req);
					req = NULL;
					ssd->request_queue = NULL;
					ssd->request_tail = NULL;
					ssd->request_queue_length--;
				}
				else
				{
					ssd->request_queue = req->next_node;
					pre_node = req;
					req = req->next_node;
					free(pre_node->need_distr_flag);
					pre_node->need_distr_flag=NULL;
					free((void *)pre_node);
					pre_node = NULL;
					ssd->request_queue_length--;
				}
			}
			else
			{
				if(req->next_node == NULL)
				{
					pre_node->next_node = NULL;
					free(req->need_distr_flag);
					req->need_distr_flag=NULL;
					free(req);
					req = NULL;
					ssd->request_tail = pre_node;
					ssd->request_queue_length--;
				}
				else
				{
					pre_node->next_node = req->next_node;
					free(req->need_distr_flag);
					req->need_distr_flag=NULL;
					free((void *)req);
					req = pre_node->next_node;
					ssd->request_queue_length--;
				}
			}
		}
		else
		{
			//printf("Rsponse time = 0!\n");
			flag=0;
			while(sub != NULL)
			{
				if(start_time == 0)
					start_time = sub->begin_time;
				if(start_time > sub->begin_time)
					start_time = sub->begin_time;
				if(end_time < sub->complete_time)
					end_time = sub->complete_time;
				if((sub->current_state == SR_COMPLETE)||((sub->next_state==SR_COMPLETE)&&(sub->next_state_predict_time<=ssd->current_time)))	// if any sub-request is not completed, the request is not completed
				{
					flag = 1;
					sub = sub->next_subs;
				}
				else
				{
					flag=0;
					break;
				}
			}
			/*if((flag == 0) && (req->MergeFlag == 1) && (req->all == 0)){
				flag = 1;
			}
			if((flag == 0) &&(req->all != 0) && (req->now == 0)){
				flag = 1;
			}*/
			if((flag == 0) && (req->subs == NULL)){
				flag = 1;
			}
			if(flag == 1 && req->completeFlag == 0)
			{	
				/*if(req->all != 0 && req->now == 0){
					start_time = req->time;
					end_time = start_time + 1000;
				}else if(req->all == 0 && req->MergeFlag == 1){
					start_time = req->time;
					end_time = start_time + 1000;
				}*/
				if(req->subs == NULL){
					start_time = req->time;
					end_time = start_time + 1000;
				}
					
				req->completeFlag = 1;
				//fprintf(ssd->outputfile,"%10I64u %10u %6u %2u %16I64u %16I64u %10I64u\n",req->time,req->lsn, req->size, req->operation, start_time, end_time, end_time-req->time);
				fprintf(ssd->outputfile,"%16lld %10d %6d %2d %16lld %16lld %10lld",req->time,req->lsn, req->size, req->operation, start_time, end_time, end_time-req->time);
				fprintf(ssd->outputfile," %lu %u %u %u",ssd->direct_erase_count + ssd->normal_erase, 0, 0,ssd->moved_page_count);
				fprintf(ssd->outputfile," %u\n",req->operation);
				fflush(ssd->outputfile);
				ssd->completed_request_count++;
				if(ssd->completed_request_count%10000 == 0){
					printf("completed requests: %ld, max_queue_depth: %d, ", ssd->completed_request_count, ssd->max_queue_depth);
					printf("free_lsb: %ld, free_csb: %ld, free_msb: %ld\n", ssd->free_lsb_count, ssd->free_csb_count, ssd->free_msb_count);
					ssd->max_queue_depth = 0;
					statistic_output_easy(ssd, ssd->completed_request_count);
					ssd->newest_read_avg = 0;
					ssd->newest_write_avg = 0;
					ssd->newest_write_avg_l = 0;
					ssd->newest_read_request_count = 0;
					ssd->newest_write_request_count = 0;
					ssd->newest_write_lsb_count = 0;
					ssd->newest_write_csb_count = 0;
					ssd->newest_write_msb_count = 0;
					ssd->newest_write_request_completed_with_same_type_pages_l = 0;
					ssd->newest_write_request_num_l= 0;
					ssd->newest_req_with_lsb_l = 0;
					ssd->newest_req_with_csb_l = 0;
					ssd->newest_req_with_msb_l = 0;
					ssd->newest_write_request_completed_with_same_type_pages = 0;
					ssd->newest_req_with_lsb = 0;
					ssd->newest_req_with_csb = 0;
					ssd->newest_req_with_msb = 0;
					//***************************************************************************
					int channel;
					int this_day;
					this_day = (int)(ssd->current_time/1000000000/3600/24);
					if(this_day>ssd->time_day){
						printf("Day %d begin......\n", this_day);
						ssd->time_day = this_day;
						if((ssd->parameter->dr_switch==1)&&(this_day%ssd->parameter->dr_cycle==0)){
							for(channel=0;channel<ssd->parameter->channel_number;channel++){
								dr_for_channel(ssd, channel);
								}
							}
						/*
						if((ssd->parameter->turbo_mode==2)&&(this_day%2==1)){
							printf("Enter turbo-mode.....\n");
							ssd->parameter->lsb_first_allocation = 1;
							ssd->parameter->fast_gc = 1;
							}
						else{
							//printf("Exist turbo-mode....\n");
							ssd->parameter->lsb_first_allocation = 0;
							ssd->parameter->fast_gc = 0;
							}
							*/
						}
					//***************************************************************************
					}
				
				if(debug_0918){
					printf("completed requests: %ld\n", ssd->completed_request_count);
					}
				if(end_time-start_time==0)
				{
					printf("the response time is 0?? position 2\n");
					//getchar();
				}
				if (req->operation==READ)
				{
					ssd->read_request_count++;
					ssd->read_avg=ssd->read_avg+(end_time-req->time);
					//=============================================
					ssd->newest_read_request_count++;
					ssd->newest_read_avg = ssd->newest_read_avg+(end_time-req->time);
					//==============================================
				} 
				else
				{
					ssd->write_request_count++;
					ssd->write_avg=ssd->write_avg+(end_time-req->time);
					//=============================================
					ssd->newest_write_request_count++;
					ssd->newest_write_avg = ssd->newest_write_avg+(end_time-req->time);
					ssd->last_write_lat = end_time-req->time;
					ssd->last_ten_write_lat[ssd->write_lat_anchor] = end_time-req->time;
					ssd->write_lat_anchor = (ssd->write_lat_anchor+1)%10;
					
					//--------------------------------------------
					int new_flag = 1;
					int origin, actual_type;
					int num_of_sub = 1;
					/*struct sub_request *next_sub_a;
					next_sub_a = req->subs;
					origin = next_sub_a->allocated_page_type;
					actual_type = next_sub_a->allocated_page_type;
					next_sub_a = next_sub_a->next_subs;
					while(next_sub_a!=NULL){
						num_of_sub++;
						if(next_sub_a->allocated_page_type > actual_type){
							actual_type = next_sub_a->allocated_page_type;
							}
						if(next_sub_a->allocated_page_type != origin){
							new_flag = 0;
							}
						next_sub_a = next_sub_a->next_subs;
						}
					if(num_of_sub>1){
						ssd->write_request_count_l++;
						ssd->newest_write_request_num_l++;
						ssd->newest_write_avg_l = ssd->newest_write_avg_l+(end_time-req->time);
						ssd->write_avg_l = ssd->write_avg_l+(end_time-req->time);
						}
					if(new_flag==1){
						ssd->newest_write_request_completed_with_same_type_pages++;
						if(num_of_sub>1){
							ssd->newest_write_request_completed_with_same_type_pages_l++;
							}
						if(origin==1){
							ssd->newest_msb_request_a++;
							}
						else if(origin==0){
							ssd->newest_lsb_request_a++;
							}
						else{
							ssd->newest_csb_request_a++;
							}
						}
					if(actual_type==TARGET_LSB){
						ssd->newest_req_with_lsb++;
						if(num_of_sub>1){
							ssd->newest_req_with_lsb_l++;
							}
						}
					else if(actual_type==TARGET_CSB){
						ssd->newest_req_with_csb++;
						if(num_of_sub>1){
							ssd->newest_req_with_csb_l++;
							}
						}
					else{
						ssd->newest_req_with_msb++;
						if(num_of_sub>1){
							ssd->newest_req_with_msb_l++;
							}
						}*/
					//-------------------------------------------
					
					//==============================================
				}
				//if(req->now == req->all){
					delete_req(ssd, &pre_node, &req);
				//}
				
			}else if(flag == 1 && req->completeFlag == 1 && req->now == req->all){
				//delete_req(ssd, &pre_node, &req);
			}
			else
			{	
				pre_node = req;
				req = req->next_node;
			}
		}		
	}
}


/*******************************************************************************
*statistic_output()������Ҫ�����������һ����������ش�����Ϣ��
*1�������ÿ��plane�Ĳ���������plane_erase���ܵĲ���������erase
*2����ӡmin_lsn��max_lsn��read_count��program_count��ͳ����Ϣ���ļ�outputfile�С�
*3����ӡ��ͬ����Ϣ���ļ�statisticfile��
*******************************************************************************/
void statistic_output(struct ssd_info *ssd)
{
	unsigned int lpn_count=0,i,j,k,m,p,erase=0,plane_erase=0;
	double gc_energy=0.0;
	extern float aveber ;
#ifdef DEBUG
	printf("enter statistic_output,  current time:%lld\n",ssd->current_time);
#endif

	for(i=0;i<ssd->parameter->channel_number;i++)
	{
		for(j=0;j<ssd->parameter->chip_channel[0];j++)
		{
			for(k=0;k<ssd->parameter->die_chip;k++)
			{
				for(p=0;p<ssd->parameter->plane_die;p++)
				{
					plane_erase=0;
					for(m=0;m<ssd->parameter->block_plane;m++)
					{
						if(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[p].blk_head[m].erase_count>0)
						{
							erase=erase+ssd->channel_head[i].chip_head[j].die_head[k].plane_head[p].blk_head[m].erase_count;
							plane_erase+=ssd->channel_head[i].chip_head[j].die_head[k].plane_head[p].blk_head[m].erase_count;
						}
					}
					fprintf(ssd->outputfile,"the %d channel, %d chip, %d die, %d plane has : %13d erase operations\n",i,j,k,p,plane_erase);
					fprintf(ssd->statisticfile,"the %d channel, %d chip, %d die, %d plane has : %13d erase operations\n",i,j,k,p,plane_erase);
				}
			}
		}
	}
	aveber = (index1 * 0.002013 + index2 * 0.000607) / (index1 + index2);
	fprintf(ssd->outputfile,"\n");
	fprintf(ssd->outputfile,"\n");
	fprintf(ssd->outputfile,"---------------------------statistic data---------------------------\n");	 
	fprintf(ssd->outputfile,"min lsn: %13d\n",ssd->min_lsn);	
	fprintf(ssd->outputfile,"max lsn: %13d\n",ssd->max_lsn);
	fprintf(ssd->outputfile,"read count: %13ld\n",ssd->read_count);	  
	fprintf(ssd->outputfile,"program count: %13ld",ssd->program_count);	
	fprintf(ssd->outputfile,"                        include the flash write count leaded by read requests\n");
	fprintf(ssd->outputfile,"the read operation leaded by un-covered update count: %13d\n",ssd->update_read_count);
	fprintf(ssd->outputfile,"erase count: %13ld\n",ssd->erase_count);
	fprintf(ssd->outputfile,"direct erase count: %13ld\n",ssd->direct_erase_count);
	fprintf(ssd->outputfile,"copy back count: %13ld\n",ssd->copy_back_count);
	fprintf(ssd->outputfile,"multi-plane program count: %13ld\n",ssd->m_plane_prog_count);
	fprintf(ssd->outputfile,"multi-plane read count: %13ld\n",ssd->m_plane_read_count);
	fprintf(ssd->outputfile,"interleave write count: %13ld\n",ssd->interleave_count);
	fprintf(ssd->outputfile,"interleave read count: %13ld\n",ssd->interleave_read_count);
	fprintf(ssd->outputfile,"interleave two plane and one program count: %13ld\n",ssd->inter_mplane_prog_count);
	fprintf(ssd->outputfile,"interleave two plane count: %13ld\n",ssd->inter_mplane_count);
	fprintf(ssd->outputfile,"gc copy back count: %13ld\n",ssd->gc_copy_back);
	fprintf(ssd->outputfile,"write flash count: %13ld\n",ssd->write_flash_count);
	//=================================================================
	fprintf(ssd->outputfile,"write LSB count: %13ld\n",ssd->write_lsb_count);
	fprintf(ssd->outputfile,"write MSB count: %13ld\n",ssd->write_msb_count);
	//=================================================================
	fprintf(ssd->outputfile,"interleave erase count: %13ld\n",ssd->interleave_erase_count);
	fprintf(ssd->outputfile,"multiple plane erase count: %13ld\n",ssd->mplane_erase_conut);
	fprintf(ssd->outputfile,"interleave multiple plane erase count: %13ld\n",ssd->interleave_mplane_erase_count);
	fprintf(ssd->outputfile,"read request count: %13d\n",ssd->read_request_count);
	fprintf(ssd->outputfile,"write request count: %13d\n",ssd->write_request_count);
	fprintf(ssd->outputfile,"read request average size: %13f\n",ssd->ave_read_size);
	fprintf(ssd->outputfile,"write request average size: %13f\n",ssd->ave_write_size);
	fprintf(ssd->outputfile,"read request average response time: %lld\n",ssd->read_avg/ssd->read_request_count);
	fprintf(ssd->outputfile,"write request average response time: %lld\n",ssd->write_avg/ssd->write_request_count);
	fprintf(ssd->outputfile,"buffer read hits: %13ld\n",ssd->dram->buffer->read_hit);
	fprintf(ssd->outputfile,"buffer read miss: %13ld\n",ssd->dram->buffer->read_miss_hit);
	fprintf(ssd->outputfile,"buffer write hits: %13ld\n",ssd->dram->buffer->write_hit);
	fprintf(ssd->outputfile,"buffer write miss: %13ld\n",ssd->dram->buffer->write_miss_hit);
	fprintf(ssd->outputfile,"erase: %13d\n",erase);
	fprintf(ssd->outputfile,"sub_request_all: %13ld, sub_request_success: %13ld\n", ssd->sub_request_all, ssd->sub_request_success);
	fflush(ssd->outputfile);

	fclose(ssd->outputfile);


	fprintf(ssd->statisticfile,"\n");
	fprintf(ssd->statisticfile,"\n");
	fprintf(ssd->statisticfile,"---------------------------statistic data---------------------------\n");
	fprintf(ssd->statisticfile,"page_move_count: %d\n",ssd->page_move_count);
	fprintf(ssd->statisticfile,"evtion: %lld\n",ssd->gc_evction);
	fprintf(ssd->statisticfile,"only page_move_count and erase: %d %d\n",ssd->page_move_count_1, ssd->page_move_count_erase);
	fprintf(ssd->statisticfile,"wl_page_move_count: %d\n",ssd->wl_page_move_count);	
	fprintf(ssd->statisticfile,"rr_page_move_count: %d\n",ssd->rr_page_move_count);	
	fprintf(ssd->statisticfile,"min lsn: %13d\n",ssd->min_lsn);	
	fprintf(ssd->statisticfile,"max lsn: %13d\n",ssd->max_lsn);
	fprintf(ssd->statisticfile,"read count: %13ld\n",ssd->read_count);	  
	fprintf(ssd->statisticfile,"program count: %13ld",ssd->program_count);	  
	fprintf(ssd->statisticfile,"                        include the flash write count leaded by read requests\n");
	fprintf(ssd->statisticfile,"the read operation leaded by un-covered update count: %13d\n",ssd->update_read_count);
	fprintf(ssd->statisticfile,"wl_request: %13d\n",ssd->wl_request);
	fprintf(ssd->statisticfile,"erase count: %13ld\n",ssd->erase_count);	
	fprintf(ssd->statisticfile,"rr_erase count: %13d\n",ssd->rr_erase_count);
	fprintf(ssd->statisticfile,"wl_erase count: %13d\n",ssd->wl_erase_count);	
	fprintf(ssd->statisticfile,"normal_erase count: %13d\n",ssd->normal_erase);
	fprintf(ssd->statisticfile,"direct erase count: %13ld\n",ssd->direct_erase_count);
	fprintf(ssd->statisticfile,"copy back count: %13ld\n",ssd->copy_back_count);
	fprintf(ssd->statisticfile,"multi-plane program count: %13ld\n",ssd->m_plane_prog_count);
	fprintf(ssd->statisticfile,"multi-plane read count: %13ld\n",ssd->m_plane_read_count);
	fprintf(ssd->statisticfile,"interleave count: %13ld\n",ssd->interleave_count);
	fprintf(ssd->statisticfile,"interleave read count: %13ld\n",ssd->interleave_read_count);
	fprintf(ssd->statisticfile,"interleave two plane and one program count: %13ld\n",ssd->inter_mplane_prog_count);
	fprintf(ssd->statisticfile,"interleave two plane count: %13ld\n",ssd->inter_mplane_count);
	fprintf(ssd->statisticfile,"gc copy back count: %13ld\n",ssd->gc_copy_back);
	fprintf(ssd->statisticfile,"write flash count: %13ld\n",ssd->write_flash_count);
	//=================================================================
	fprintf(ssd->statisticfile,"write LSB count: %13ld\n",ssd->write_lsb_count);
	fprintf(ssd->statisticfile,"write CSB count: %13ld\n",ssd->write_csb_count);
	fprintf(ssd->statisticfile,"write MSB count: %13ld\n",ssd->write_msb_count);
	//=================================================================
	fprintf(ssd->statisticfile,"waste page count: %13ld\n",ssd->waste_page_count);
	fprintf(ssd->statisticfile,"interleave erase count: %13ld\n",ssd->interleave_erase_count);
	fprintf(ssd->statisticfile,"multiple plane erase count: %13ld\n",ssd->mplane_erase_conut);
	fprintf(ssd->statisticfile,"interleave multiple plane erase count: %13ld\n",ssd->interleave_mplane_erase_count);
	fprintf(ssd->statisticfile,"read request count: %13d\n",ssd->read_request_count);
	fprintf(ssd->statisticfile,"write request count: %13d\n",ssd->write_request_count);
	fprintf(ssd->statisticfile,"read request average size: %13f\n",ssd->ave_read_size);
	fprintf(ssd->statisticfile,"write request average size: %13f\n",ssd->ave_write_size);
	fprintf(ssd->statisticfile,"read request average response time: %llu\n",ssd->read_avg/((unsigned long long)ssd->read_request_count));
	fprintf(ssd->statisticfile,"write request average response time: %llu\n",ssd->write_avg/((unsigned long long)ssd->write_request_count));
	fprintf(ssd->statisticfile,"degrade count: %llu\n", ssd->degradeCount);
	fprintf(ssd->statisticfile,"read response time: %llu\n", ssd->read_avg);
	fprintf(ssd->statisticfile,"write response time: %llu\n", ssd->write_avg);
	fprintf(ssd->statisticfile,"I/O response time: %llu\n",ssd->write_avg + ssd->read_avg);
	if(ssd->write_request_count_l==0){
		fprintf(ssd->statisticfile,"large write request average response time: 0\n");
		}
	else{
		fprintf(ssd->statisticfile,"large write request average response time: %llu\n",ssd->write_avg_l/((unsigned long long)ssd->write_request_count_l));
		}
	fprintf(ssd->statisticfile,"buffer read hits: %13ld\n",ssd->dram->buffer->read_hit);
	fprintf(ssd->statisticfile,"buffer read miss: %13ld\n",ssd->dram->buffer->read_miss_hit);
	fprintf(ssd->statisticfile,"buffer write hits: %13ld\n",ssd->dram->buffer->write_hit);
	fprintf(ssd->statisticfile,"buffer write miss: %13ld\n",ssd->dram->buffer->write_miss_hit);
	fprintf(ssd->statisticfile,"buffer write free: %13ld\n",ssd->dram->buffer->write_free);
	fprintf(ssd->statisticfile,"buffer eject: %13ld\n",ssd->dram->buffer->eject);
	fprintf(ssd->statisticfile,"erase: %13d\n",erase);

	fprintf(ssd->statisticfile, "RRcount: %13d\n", RRcount);

	fprintf(ssd->statisticfile,"sub_request_all: %13ld, sub_request_success: %13ld\n", ssd->sub_request_all, ssd->sub_request_success);
	fprintf(ssd->statisticfile, "index1: %13d\n", index1);
	fprintf(ssd->statisticfile, "index2: %13d\n", index2);
	fprintf(ssd->statisticfile, "aveber: %9f\n", aveber);
	fprintf(ssd->statisticfile,"\n\n\n");

	double available_capacity=0;
	
	fprintf(ssd->statisticfile,"\ncap\n");
	for (i = 0; i < ssd->parameter->channel_number; i++)
	{
		double available_capacity_channel = 0;
		for (j = 0; j < ssd->parameter->chip_num / ssd->parameter->channel_number; j++)
		{
			for (k = 0; k < ssd->parameter->die_chip; k++)
			{
				for (m = 0; m < ssd->parameter->plane_die; m++)
				{
					available_capacity += ssd->channel_head[i].chip_head[j].die_head[k].plane_head[m].free_page;
					available_capacity_channel += ssd->channel_head[i].chip_head[j].die_head[k].plane_head[m].free_page;
				}
				
			}
			
		}
		fprintf(ssd->statisticfile,"%f\t", available_capacity_channel/(ssd->parameter->chip_channel[0]*ssd->parameter->die_chip*ssd->parameter->plane_die*ssd->parameter->block_plane*ssd->parameter->page_block));
	}
	fprintf(ssd->statisticfile,"\n");
	printf("available capacity:%f\n", available_capacity/(ssd->parameter->channel_number*ssd->parameter->chip_channel[0]*ssd->parameter->die_chip*ssd->parameter->plane_die*ssd->parameter->block_plane*ssd->parameter->page_block)); 
	printf("%lld\t%lld\n", ssd->raidWriteCount[0], ssd->raidWriteCount[1]);
	
	fprintf(ssd->statisticfile,"number of vaild page be changed: %ld\n",ssd->invaild_page_of_change);
	ssd->invaild_page_of_all += ssd->invaild_page_of_change;
	fprintf(ssd->statisticfile,"number of vaild page when gc be created: %ld\n",ssd->invaild_page_of_all);
	fprintf(ssd->statisticfile,"rate is : %lf\n", ((double)(ssd->invaild_page_of_change)) / ((double)(ssd->invaild_page_of_all)));
	fprintf(ssd->statisticfile,"number of frequently write: %ld\n",ssd->frequently_count);
	fprintf(ssd->statisticfile,"number of not frequently write: %ld\n",ssd->Nofrequently_count);
	fprintf(ssd->statisticfile,"when ssdsim finish the number of ssd->request: %d\n",ssd->gc_request);
	fflush(ssd->statisticfile);
	printf("%lld %lld %lld %lld\n",  ssd->read1, ssd->read2, ssd->read3, ssd->read4);
	fprintf(ssd->statisticfile,"parityChange %lld\n", ssd->parityChange);

	fprintf(ssd->statisticfile,"all gcInterval128 %lld\n", ssd->gcInterval128);
	fprintf(ssd->statisticfile,"gcInterval128 %lld\n", ssd->gcInterval128 / 128);
	fprintf(ssd->statisticfile,"all  gcInterval256 %lld\n", ssd->gcInterval256);
	fprintf(ssd->statisticfile,"gcInterval256 %lld\n", ssd->gcInterval256 / 256);

	
	
	printf("pageMoveRaid %lld\n", ssd->pageMoveRaid);
	printf("parityChange %lld\n", ssd->parityChange);
	
	fprintf(ssd->statisticfile,"channel avg block\n");
	for(i = 0; i < ssd->parameter->channel_number ; ++i){
		fprintf(ssd->statisticfile,"%f\t", ((double)(ssd->channel_head[i].blockCountR + ssd->channel_head[i].blockCountW)) / ((double)(ssd->channel_head[i].completeCountR + ssd->channel_head[i].completeCountW)));
	}
	fprintf(ssd->statisticfile,"\n");
	fprintf(ssd->statisticfile,"channel count 2\n");
	for(i = 0; i < ssd->parameter->channel_number ; ++i){
		fprintf(ssd->statisticfile,"%lld\t",ssd->channel_head[i].completeCountW + ssd->channel_head[i].completeCountR);;
	}
	fprintf(ssd->statisticfile,"\n");
	fprintf(ssd->statisticfile,"channel count 2 read \n");
	for(i = 0; i < ssd->parameter->channel_number ; ++i){
		fprintf(ssd->statisticfile,"%lld\t",ssd->channel_head[i].completeCountR);
	}
	fprintf(ssd->statisticfile,"\n");
	fprintf(ssd->statisticfile,"channel count 2 write\n");
	for(i = 0; i < ssd->parameter->channel_number ; ++i){
		fprintf(ssd->statisticfile,"%lld\t",ssd->channel_head[i].completeCountW);
	}
	fprintf(ssd->statisticfile,"\n");

	fprintf(ssd->statisticfile,"channel R wait time\n");
	unsigned long long allTime = 0;
	for(i = 0; i < ssd->parameter->channel_number ; ++i){
		allTime +=  ssd->chanSubRWriteTime[i] ;
		fprintf(ssd->statisticfile,"%lld\t", ssd->chanSubRWriteTime[i]);
	}
	if(ssd->subRWaitCount)
		fprintf(ssd->statisticfile,"\nall wait time %lld\n", allTime / ssd->subRWaitCount);
	if(ssd->subRWaitCount_2 && ssd->subRWaitCount_2)
		fprintf(ssd->statisticfile,"all  write wait time %lld %lld %lld\n", ssd->chanSubRWriteTime_2 / ssd->subRWaitCount_2,  ssd->chanSubRWriteTime_2 , ssd->subRWaitCount_2);
	fprintf(ssd->statisticfile,"\n");
	fprintf(ssd->statisticfile,"\ngc\n");
	for(i = 0; i < ssd->parameter->channel_number ; ++i){
		for(j= 0; j < ssd->parameter->chip_channel[0];j++){
			fprintf(ssd->statisticfile,"%lld\t",ssd->chipGc[i * ssd->parameter->chip_channel[0] + j]);
		}
	}
	fprintf(ssd->statisticfile,"\n");
	fprintf(ssd->statisticfile, "error count\t");
	for (i = 0; i < 7; i++){
		fprintf(ssd->statisticfile, "%lld\t", ssd->errDegradeCount[i]);
	}
	fprintf(ssd->statisticfile,"\n");

	fprintf(ssd->statisticfile, "channel count\n");
	for(i = 0; i < ssd->parameter->channel_number ; ++i){
		for(j= 0; j < ssd->parameter->chip_channel[0];j++){
			fprintf(ssd->statisticfile,"%lld\t",ssd->chipWrite[i * ssd->parameter->chip_channel[0] + j]);
		}
	}
	fprintf(ssd->statisticfile,"\n");
	fprintf(ssd->statisticfile,"change %lld %lld %lld\n", ssd->changeCount[0], ssd->changeCount[1], ssd->changeCount[2]);
	
	int cacheCount = 0;
	struct buffer_group *buffer = ssd->dram->buffer->buffer_head;
	while(buffer){
		for(int i = 0; i < ssd->stripe.all; i++){
			if(buffer->accessCount[i] != 0){
				cacheCount++;
			}
		}
		buffer = buffer->LRU_link_next;
	}
	fprintf(ssd->statisticfile, "cacheCount is %d\n", cacheCount);
	fprintf(ssd->statisticfile, "read count chann\n");
	for (i = 0; i < ssd->parameter->channel_number; i++){
		fprintf(ssd->statisticfile, "%lld\t", ssd->chanRs[i]);
	}
	fprintf(ssd->statisticfile,"\n");
	fprintf(ssd->statisticfile, "ssd->cacheFail %lld\n", ssd->cacheFail);
	fprintf(ssd->statisticfile, "ssd->cacheFailAll %lld\n", ssd->cacheFailAll);
	fprintf(ssd->statisticfile,"use efficency %f\n",(double)ssd->cacheHit/((double)(ssd->cachedoNotHit + ssd->cacheHit)));
	fprintf(ssd->statisticfile,"moti1 %f %f\n",(double)ssd->max/((double)(ssd->countMoti)), (double)ssd->min/((double)(ssd->countMoti)));
	fprintf(ssd->statisticfile,"moti2 %f\n",(double)ssd->distanceMoti/((double)(ssd->distanceCountMoti)));
	fprintf(ssd->statisticfile,"moti3 %f\n",(double)ssd->allCacheCount/((double)(ssd->eCount)));
	fclose(ssd->statisticfile);
	fclose(ssd->raidOutfile);
	fclose(ssd->gcIntervalFile);
	fclose(ssd->gcCreateRequest);
	fclose(ssd->motivateFile);
	printf("\n");
	
	unsigned long long chan[ssd->parameter->channel_number];
	for(i = 0; i < ssd->parameter->channel_number ; ++i){
		chan[i] = 0;
	}
	for(i = 0; i < ssd->stripe.allStripe ; ++i){
		if(ssd->trip2Page[i].using){
			chan[ssd->trip2Page[i].Pchannel]++;
		}
	}
	printf("parity count\n");
	for(i = 0; i < ssd->parameter->channel_number ; ++i){
		printf("%lld(%lld)\t", chan[i], ssd->paritycount[i]);
	}
	printf("\nparity count\n");
}

void statistic_output_easy(struct ssd_info *ssd, unsigned long completed_requests_num){
	unsigned int lpn_count=0,i,j,k,m,erase=0,plane_erase=0;
	double gc_energy=0.0;
#ifdef DEBUG
	fprintf(ssd->debugfile,"enter statistic_output,  current time:%lld\n",ssd->current_time);
	//printf("enter statistic_output,  current time:%lld\n",ssd->current_time);
#endif
	unsigned long read_avg_lat, write_avg_lat, write_avg_lat_l;
	if(ssd->newest_read_request_count==0){
		read_avg_lat=0;
		}
	else{
		read_avg_lat=ssd->newest_read_avg/ssd->newest_read_request_count;
		}
	if(ssd->newest_write_request_count==0){
		write_avg_lat=0;
		}
	else{
		write_avg_lat=ssd->newest_write_avg/ssd->newest_write_request_count;
		}
	if(ssd->newest_write_request_num_l==0){
		write_avg_lat_l=0;
		}
	else{
		write_avg_lat_l = ssd->newest_write_avg_l/ssd->newest_write_request_num_l;
		}
	fprintf(ssd->statisticfile, "%ld, %16lld, %13ld, %13ld, %13ld, %13ld, %13ld, %13ld, ", completed_requests_num, ssd->current_time, ssd->erase_count, read_avg_lat, write_avg_lat,ssd->newest_write_lsb_count,ssd->newest_write_csb_count,ssd->newest_write_msb_count);
	fprintf(ssd->statisticfile, "%13ld, %13d, %13ld, %13d, %13d, ", ssd->fast_gc_count, ssd->moved_page_count, ssd->free_lsb_count, ssd->newest_read_request_count, ssd->newest_write_request_count);
	fprintf(ssd->statisticfile, "%13d, %13d, %13d, %13d, ", ssd->newest_write_request_completed_with_same_type_pages, ssd->newest_req_with_lsb, ssd->newest_req_with_csb, ssd->newest_req_with_msb);
	fprintf(ssd->statisticfile, "% 13d, %13d, %13d, %13d, %13d, %13ld\n", ssd->newest_write_request_num_l, ssd->newest_write_request_completed_with_same_type_pages_l, ssd->newest_req_with_lsb_l, ssd->newest_req_with_csb_l, ssd->newest_req_with_msb_l, write_avg_lat_l);
	
	//fprintf(ssd->statisticfile, "%13d, %13d, %13d\n", ssd->newest_write_request_completed_with_same_type_pages, ssd->newest_write_lsb_count, ssd->newest_write_msb_count);
	//fprintf(ssd->statisticfile,"\n\n");
	fflush(ssd->statisticfile);

	printf("change %lld %lld %lld\n", ssd->changeCount[0], ssd->changeCount[1], ssd->changeCount[2]);
}


/***********************************************************************************
*����ÿһҳ��״̬�����ÿһ��Ҫ��������ҳ����Ŀ��Ҳ����һ����������Ҫ��������ҳ��ҳ��
************************************************************************************/
unsigned int size(unsigned int stored)
{
	unsigned int i,total=0,mask=0x80000000;

	#ifdef DEBUG
	printf("enter size\n");
	#endif
	for(i=1;i<=32;i++)
	{
		if(stored & mask) total++;
		stored<<=1;
	}
	#ifdef DEBUG
	    printf("leave size\n");
    #endif
    return total;
}


/*********************************************************
*transfer_size()���������þ��Ǽ�������������Ҫ������size
*�����е���������first_lpn��last_lpn�������ر��������Ϊ��
*��������º��п��ܲ��Ǵ���һ��ҳ���Ǵ���һҳ��һ���֣���
*Ϊlsn�п��ܲ���һҳ�ĵ�һ����ҳ��
*********************************************************/
unsigned int transfer_size(struct ssd_info *ssd,int need_distribute,unsigned int lpn,struct request *req)
{
	unsigned int first_lpn,last_lpn,state,trans_size;
	unsigned int mask=0,offset1=0,offset2=0;

	first_lpn=req->lsn/ssd->parameter->subpage_page;
	last_lpn=(req->lsn+req->size-1)/ssd->parameter->subpage_page;

	mask=FULL_PAGE_STATE(ssd->parameter->subpage_page);
	state=mask;
	if(lpn==first_lpn)
	{
		offset1=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-req->lsn);
		state=state&(0xffffffff<<offset1);
	}
	if(lpn==last_lpn)
	{
		offset2=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-(req->lsn+req->size));
		state=state&(~(0xffffffff<<offset2));
	}

	trans_size=size(state&need_distribute);

	return trans_size;
}


/**********************************************************************************************************  
*int64_t find_nearest_event(struct ssd_info *ssd)       
*Ѱ����������������絽����¸�״̬ʱ��,���ȿ��������һ��״̬ʱ�䣬���������¸�״̬ʱ��С�ڵ��ڵ�ǰʱ�䣬
*˵��������������Ҫ�鿴channel���߶�Ӧdie����һ״̬ʱ�䡣Int64���з��� 64 λ�����������ͣ�ֵ���ͱ�ʾֵ����
*-2^63 ( -9,223,372,036,854,775,808)��2^63-1(+9,223,372,036,854,775,807 )֮����������洢�ռ�ռ 8 �ֽڡ�
*channel,die���¼���ǰ�ƽ��Ĺؼ����أ������������ʹ�¼�������ǰ�ƽ���channel��die�ֱ�ص�idle״̬��die�е�
*������׼������
***********************************************************************************************************/
int64_t find_nearest_event(struct ssd_info *ssd) 
{
	unsigned int i,j;
	int64_t time=MAX_INT64;
	int64_t time1=MAX_INT64;
	int64_t time2=MAX_INT64;

	for (i=0;i<ssd->parameter->channel_number;i++)
	{
		if (ssd->channel_head[i].next_state==CHANNEL_IDLE)
			if(time1>ssd->channel_head[i].next_state_predict_time)
				if (ssd->channel_head[i].next_state_predict_time>ssd->current_time)    
					time1=ssd->channel_head[i].next_state_predict_time; //next state time
		for (j=0;j<ssd->parameter->chip_channel[i];j++)
		{
			if ((ssd->channel_head[i].chip_head[j].next_state==CHIP_IDLE)||(ssd->channel_head[i].chip_head[j].next_state==CHIP_DATA_TRANSFER))
				if(time2>ssd->channel_head[i].chip_head[j].next_state_predict_time)
					if (ssd->channel_head[i].chip_head[j].next_state_predict_time>ssd->current_time)    
						time2=ssd->channel_head[i].chip_head[j].next_state_predict_time;	
		}   
	} 

	/*****************************************************************************************************
	 *timeΪ���� A.��һ״̬ΪCHANNEL_IDLE����һ״̬Ԥ��ʱ�����ssd��ǰʱ���CHANNEL����һ״̬Ԥ��ʱ��
	 *           B.��һ״̬ΪCHIP_IDLE����һ״̬Ԥ��ʱ�����ssd��ǰʱ���DIE����һ״̬Ԥ��ʱ��
	 *		     C.��һ״̬ΪCHIP_DATA_TRANSFER����һ״̬Ԥ��ʱ�����ssd��ǰʱ���DIE����һ״̬Ԥ��ʱ��
	 *CHIP_DATA_TRANSFER��׼����״̬�������Ѵӽ��ʴ�����register����һ״̬�Ǵ�register����buffer�е���Сֵ 
	 *ע����ܶ�û������Ҫ���time����ʱtime����0x7fffffffffffffff ��
	*****************************************************************************************************/
	time=(time1>time2)?time2:time1;
	if(ssd->cpu_current_state != CPU_IDLE && ssd->cpu_next_state_predict_time <= time)
			time = ssd->cpu_next_state_predict_time;
	return time;
}

/***********************************************
*free_all_node()���������þ����ͷ���������Ľڵ�
************************************************/
void free_all_node(struct ssd_info *ssd)
{
	unsigned int i,j,k,l,n;
	struct buffer_group *pt=NULL;
	struct direct_erase * erase_node=NULL;
	unsigned long long StripeNum =  ssd->parameter->page_block * ssd->parameter->block_plane * ssd->parameter->plane_die * ssd->parameter->die_chip;
	unsigned long long chipNum = ssd->parameter->chip_channel[0] * ssd->parameter->channel_number;
	printf("free_all_node\n");
	for (i=0;i<ssd->parameter->channel_number;i++)
	{
		for (j=0;j<ssd->parameter->chip_channel[0];j++)
		{
			for (k=0;k<ssd->parameter->die_chip;k++)
			{
				for (l=0;l<ssd->parameter->plane_die;l++)
				{
					for (n=0;n<ssd->parameter->block_plane;n++)
					{
						free(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[n].page_head);
						ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[n].page_head=NULL;
					}
					free(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head);
					ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head=NULL;
					while(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].erase_node!=NULL)
					{
						erase_node=ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].erase_node;
						ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].erase_node=erase_node->next_node;
						free(erase_node);
						erase_node=NULL;
					}
				}
				
				free(ssd->channel_head[i].chip_head[j].die_head[k].plane_head);
				ssd->channel_head[i].chip_head[j].die_head[k].plane_head=NULL;
			}
			free(ssd->channel_head[i].chip_head[j].die_head);
			ssd->channel_head[i].chip_head[j].die_head=NULL;
		}
		free(ssd->channel_head[i].chip_head);
		ssd->channel_head[i].chip_head=NULL;
	}
	free(ssd->channel_head);
	ssd->channel_head=NULL;
	
	
	//avlTreeDestroy( ssd->dram->buffer);
	hash_destroy(ssd->dram->buffer);
	ssd->dram->buffer=NULL;


	free(ssd->chanSubLenAll);
	free(ssd->chanSubCount);
	free(ssd->chanSubWLenNow);
	free(ssd->preRequestArriveTime);
	free(ssd->chipWrite);
	for (i = 0; i < ssd->stripe.allStripe; ++i) free(ssd->trip2Page[i].location);
    free(ssd->trip2Page[0].lpn);
    free(ssd->trip2Page);
	free(ssd->page2Trip);
    free(ssd->raidBitMap);
    free(ssd->chanSubRLenNow);
    free(ssd->chanSubRWriteTime);
    free(ssd->preSubWriteLpn);
    free(ssd->chipGc);
    free(ssd->paritycount);
    free(ssd->chanRs);
	free(ssd->readBufReqQue);
	free(ssd->stripe.req);
	free(ssd->raidReq);
	free(ssd->dram->map->map_entry);
	ssd->dram->map->map_entry=NULL;
	free(ssd->dram->map);
	ssd->dram->map=NULL;
	free(ssd->dram);
	ssd->dram=NULL;
	free(ssd->parameter);
	ssd->parameter=NULL;
	
	free(ssd);
	ssd=NULL;
}


/*****************************************************************************
*make_aged()���������þ���ģ����ʵ���ù�һ��ʱ���ssd��
*��ô���ssd����Ӧ�Ĳ�����Ҫ�ı䣬�����������ʵ���Ͼ��Ƕ�ssd�и��������ĸ�ֵ��
******************************************************************************/
struct ssd_info *make_aged(struct ssd_info *ssd)
{
	unsigned int i,j,k,l,m,n,ppn;
	int threshould,flag=0;
    
	if (ssd->parameter->aged==1)
	{
		//threshold��ʾһ��plane���ж���ҳ��Ҫ��ǰ��ΪʧЧ
		threshould=(int)(ssd->parameter->block_plane*ssd->parameter->page_block*ssd->parameter->aged_ratio);  
		for (i=0;i<ssd->parameter->channel_number;i++)
			for (j=0;j<ssd->parameter->chip_channel[i];j++)
				for (k=0;k<ssd->parameter->die_chip;k++)
					for (l=0;l<ssd->parameter->plane_die;l++)
					{  
						flag=0;
						for (m=0;m<ssd->parameter->block_plane;m++)
						{  
							if (flag>=threshould)
							{
								break;
							}
							for (n=0;n<(ssd->parameter->page_block*ssd->parameter->aged_ratio+1);n++)
							{  
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].page_head[n].valid_state=0;        //��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].page_head[n].free_state=0;         //��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].page_head[n].lpn=0;  //��valid_state free_state lpn����Ϊ0��ʾҳʧЧ������ʱ�������⣬����lpn=0��������Чҳ
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].free_page_num--;
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].invalid_page_num++;
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].last_write_page++;
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].free_page--;
								flag++;
								if(n%3==0){
									ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].last_write_lsb=n;
									ssd->free_lsb_count--;
									ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].free_lsb_num--;
									//ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--;
									ssd->write_lsb_count++;
									ssd->newest_write_lsb_count++;
									ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].free_lsb_num--;
									}
								else if(n%3==2){
									ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].last_write_msb=n;
									ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].free_msb_num--;
									//ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--;
									ssd->write_msb_count++;
									ssd->free_msb_count--;
									ssd->newest_write_msb_count++;
									}
								else{
									ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].last_write_csb=n;
									ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].free_csb_num--;
									ssd->write_csb_count++;
									ssd->free_csb_count--;
									ssd->newest_write_csb_count++;
									}
								ppn=find_ppn(ssd,i,j,k,l,m,n);
							
							}
						} 
					}	 
	}  
	else
	{
		return ssd;
	}

	return ssd;
}

int get_old_zwh(struct ssd_info *ssd){
	int cn_id, cp_id, di_id, pl_id;
	printf("Enter get_old_zwh.\n");
	for(cn_id=0;cn_id<ssd->parameter->channel_number;cn_id++){
		//printf("channel %d\n", cn_id);
		for(cp_id=0;cp_id<ssd->parameter->chip_channel[0];cp_id++){
			//printf("chip %d\n", cp_id);
			for(di_id=0;di_id<ssd->parameter->die_chip;di_id++){
				//printf("die %d\n", di_id);
				for(pl_id=0;pl_id<ssd->parameter->plane_die;pl_id++){
					//printf("channel %d, chip %d, die %d, plane %d: ", cn_id, cp_id, di_id, pl_id);
					int active_block, lpn;
					unsigned int ppn;
					struct local *location;
					lpn = 0;
					while(ssd->channel_head[cn_id].chip_head[cp_id].die_head[di_id].plane_head[pl_id].free_page > (ssd->parameter->page_block*ssd->parameter->block_plane)*0.3){
						//if(cn_id==0&&cp_id==2&&di_id==0&&pl_id==0){
						//	printf("cummulating....\n");
						//	}
						if(find_active_block(ssd,cn_id,cp_id,di_id,pl_id)==FAILURE)
							{
								printf("Wrong in get_old_zwh, find_active_block\n");	
								return 0;
							}
						active_block=ssd->channel_head[cn_id].chip_head[cp_id].die_head[di_id].plane_head[pl_id].active_block;
						if(write_page(ssd,cn_id,cp_id,di_id,pl_id,active_block,&ppn)==ERROR)
							{
								return 0;
							}
						location=find_location(ssd,ppn);
						ssd->program_count++;
						ssd->channel_head[cn_id].program_count++;
						ssd->channel_head[cn_id].chip_head[cp_id].program_count++;	
						ssd->channel_head[cn_id].chip_head[cp_id].die_head[di_id].plane_head[pl_id].blk_head[active_block].page_head[location->page].lpn=0;
						ssd->channel_head[cn_id].chip_head[cp_id].die_head[di_id].plane_head[pl_id].blk_head[active_block].page_head[location->page].valid_state=0;
						ssd->channel_head[cn_id].chip_head[cp_id].die_head[di_id].plane_head[pl_id].blk_head[active_block].page_head[location->page].free_state=0;
						ssd->channel_head[cn_id].chip_head[cp_id].die_head[di_id].plane_head[pl_id].blk_head[active_block].invalid_page_num++;
						free(location);
						location=NULL;
						}
					//printf("%d\n", ssd->channel_head[cn_id].chip_head[cp_id].die_head[di_id].plane_head[pl_id].free_page);
					}
				}
			}
		}
	printf("Exit get_old_zwh.\n");
	return 0;
}



/*********************************************************************************************
*no_buffer_distribute()�����Ǵ�����ssdû��dram��ʱ��
*���Ƕ�д����Ͳ�������Ҫ��buffer����Ѱ�ң�ֱ������creat_sub_request()���������������ٴ�����
*********************************************************************************************/
struct sub_request *get_sub_request_for_recovery(struct ssd_info *ssd, int channel, int chip, int die, int plane, int block, int page, struct request* req){
	struct sub_request* sub = (struct sub_request*)malloc(sizeof(struct sub_request));                       
	alloc_assert(sub,"sub_request");
	memset(sub,0, sizeof(struct sub_request));
	struct local *location = (struct local*)malloc(sizeof(struct local));
	alloc_assert(sub,"struct local");
	memset(location,0, sizeof(struct local));

	unsigned int mask=0;
	if(ssd->parameter->subpage_page == 32){
		mask = 0xffffffff;
	}else{
		mask=FULL_PAGE_STATE(ssd->parameter->subpage_page);
	}
	
	location->channel = channel;
	location->chip = chip;
	location->die = die;
	location->plane = plane;
	location->block = block;
	location->page = page;
	
	sub->location = location;
	sub->lpn = ssd->stripe.checkLpn + 1;
	sub->size = size(mask);
	sub->ppn = 0;
	sub->state=mask;
	sub->raidNUM = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].raidID;

	sub->next_subs = req->subs;
	req->subs = sub;
	
	return sub;
}

struct sub_request* create_recovery_sub_write(struct ssd_info *ssd, int channel, int lpn, struct request* req){
	unsigned int mask=0; 
	struct sub_request* sub = get_sub_request_for_recovery(ssd, channel, 0, 0, 0, 0, 0, req);
	
	if(ssd->parameter->subpage_page == 32){
		mask = 0xffffffff;
	}else{
		mask=FULL_PAGE_STATE(ssd->parameter->subpage_page);
	}
	sub->operation = WRITE;
	sub->current_state=SR_WAIT;
	sub->current_time=ssd->current_time;
	sub->begin_time = ssd->current_time;
	sub->lpn = lpn;
	struct channel_info *p_ch = &ssd->channel_head[channel];
	if (p_ch->subs_w_tail!=NULL){
		p_ch->subs_w_tail->next_node=sub;
		p_ch->subs_w_tail=sub;
	}else{
		p_ch->subs_w_head=sub;
		p_ch->subs_w_tail=sub;
	}
	sub->completeCountR = malloc(sizeof(unsigned long long) * ssd->parameter->channel_number);
	sub->completeCountW = malloc(sizeof(unsigned long long) * ssd->parameter->channel_number);

	for(int i = 0; i < ssd->parameter->channel_number; ++i){
		sub->completeCountR[i] = ssd->channel_head[i].completeCountR;
		sub->completeCountW[i] = ssd->channel_head[i].completeCountW;
	}
	return sub;
}

/*********************************************************************************************
*no_buffer_distribute()�����Ǵ�����ssdû��dram��ʱ��
*���Ƕ�д����Ͳ�������Ҫ��buffer����Ѱ�ң�ֱ������creat_sub_request()���������������ٴ�����
*********************************************************************************************/

void move_to_head(struct ssd_info *ssd, struct buffer_group *buffer_node){
	if(ssd->dram->buffer->buffer_tail == buffer_node){				
		ssd->dram->buffer->buffer_tail = buffer_node->LRU_link_pre;
		buffer_node->LRU_link_pre->LRU_link_next = NULL;					
	}else{					
		buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;				
		buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;
	}	

	buffer_node->LRU_link_next=ssd->dram->buffer->buffer_head;	
	ssd->dram->buffer->buffer_head->LRU_link_pre=buffer_node;
	buffer_node->LRU_link_pre=NULL;				
	ssd->dram->buffer->buffer_head=buffer_node;	
}

struct buffer_group *get_last(struct ssd_info *ssd)
{
    struct buffer_group *node = ssd->dram->buffer->buffer_tail;
    if (!node) return NULL;
    ssd->dram->buffer->buffer_tail = node->LRU_link_pre;
    if (node->LRU_link_pre) node->LRU_link_pre->LRU_link_next = NULL;
    else ssd->dram->buffer->buffer_head = NULL;
    node->LRU_link_pre = node->LRU_link_next = NULL;
    return node;
}

void creat_update_sub(struct ssd_info* ssd, struct sub_request *sub){
	unsigned int invalidC = 0;
	struct sub_request *update, *preUpdate = NULL;
	int i = 0;
	
	for(i = 0; i < ssd->stripe.all - 1; ++i){
		unsigned long long lpn = ssd->trip2Page[sub->raidNUM].lpn[i];
		if(ssd->dram->map->map_entry[lpn].state != 0 && ssd->dram->map->map_entry[lpn].oldppa != -1){
			++invalidC;
		}
	}
	if(invalidC == 0){
		return;
	}
	// if(sub->raidNUM == 9154)
	// 	printf("eject,,,%d\n", sub->raidNUM);
	if(1){
		for(i = 0; i < ssd->stripe.all - 1; ++i){
			unsigned long long lpn = ssd->trip2Page[sub->raidNUM].lpn[i];
			struct local *location;
			if(ssd->dram->map->map_entry[lpn].state != 0 && ssd->dram->map->map_entry[lpn].oldppa == -1){
				ssd->read_count++;
				ssd->read1++;
				ssd->update_read_count++;

				update=(struct sub_request *)malloc(sizeof(struct sub_request));
				alloc_assert(update,"update");
				memset(update,0, sizeof(struct sub_request));

				if(update==NULL)
				{
					abort();
				}
				update->location=NULL;
				update->next_node=NULL;
				update->next_subs=NULL;
				update->update=NULL;	
				update->lpn = lpn;					
				location = find_location(ssd,ssd->dram->map->map_entry[update->lpn].pn);
				update->location = location;
				update->begin_time = ssd->current_time;
				update->current_state = SR_WAIT;
				update->current_time=MAX_INT64;
				update->next_state = SR_R_C_A_TRANSFER;
				update->next_state_predict_time=MAX_INT64;
				update->state=(ssd->dram->map->map_entry[lpn].state);
				update->size=size(update->state);
				update->ppn = ssd->dram->map->map_entry[lpn].pn;
				update->operation = READ;
				update->parent = sub;
				update->raidNUM = sub->raidNUM;
				update->completeCountR = malloc(sizeof(unsigned long long) * ssd->parameter->channel_number);
				update->completeCountW = malloc(sizeof(unsigned long long) * ssd->parameter->channel_number);
				for(int ii = 0; ii < ssd->parameter->channel_number; ++ii){
					update->completeCountR[ii] = ssd->channel_head[ii].completeCountR;
					update->completeCountW[ii] = ssd->channel_head[ii].completeCountW;
				}
				if(1){
				//if(ssd->parameter->dynamic_allocation != 2){
					++ssd->chanSubRLenNow[location->channel];
					//ssd->chanSubLenAll[location->channel] += (ssd->chanSubRLenNow[location->channel] + ssd->chanSubWLenNow[location->channel]);
					ssd->chanSubLenAll[location->channel] += (ssd->chanSubRLenNow[location->channel]);
					++ssd->chanSubCount[location->channel];
					if (ssd->channel_head[location->channel].subs_r_tail!=NULL)            /*�����µĶ����󣬲��ҹҵ�channel��subs_r_tail����β*/
					{
						ssd->channel_head[location->channel].subs_r_tail->next_node=update;
						ssd->channel_head[location->channel].subs_r_tail=update;
					} 
					else
					{
						ssd->channel_head[location->channel].subs_r_tail=update;
						ssd->channel_head[location->channel].subs_r_head=update;
					}
				}
				if(preUpdate == NULL){
					sub->update = update;
				}else{
					preUpdate->update = update;
				}
				preUpdate = update;
			}else if(ssd->dram->map->map_entry[lpn].oldppa != -1){
				location = find_location(ssd, ssd->dram->map->map_entry[lpn].oldppa);
				invaild_semi_page(ssd, location);
				free(location);
				ssd->dram->map->map_entry[lpn].oldppa = -1;
			}
		}
	}else{
		unsigned long long lpn = ssd->trip2Page[sub->raidNUM].lpn[i];
		struct local *location;
		for(i = 0; i < ssd->stripe.all - 1; ++i){
			lpn = ssd->trip2Page[sub->raidNUM].lpn[i];
			if(ssd->dram->map->map_entry[lpn].state != 0 && ssd->dram->map->map_entry[lpn].oldppa != -1){
				ssd->read_count++;
				ssd->read1++;
				ssd->update_read_count++;

				update=(struct sub_request *)malloc(sizeof(struct sub_request));
				alloc_assert(update,"update");
				memset(update,0, sizeof(struct sub_request));

				if(update==NULL)
				{
					abort();
				}
				update->location=NULL;
				update->next_node=NULL;
				update->next_subs=NULL;
				update->update=NULL;	
				update->lpn = lpn;					
				location = find_location(ssd,ssd->dram->map->map_entry[update->lpn].oldppa);
				update->location = location;
				update->begin_time = ssd->current_time;
				update->current_state = SR_WAIT;
				update->current_time=MAX_INT64;
				update->next_state = SR_R_C_A_TRANSFER;
				update->next_state_predict_time=MAX_INT64;
				update->state=(ssd->dram->map->map_entry[lpn].state);
				update->size=size(update->state);
				update->ppn = ssd->dram->map->map_entry[lpn].pn;
				update->operation = READ;
				update->parent = sub;
				update->raidNUM = sub->raidNUM;
				update->completeCountR = malloc(sizeof(unsigned long long) * ssd->parameter->channel_number);
				update->completeCountW = malloc(sizeof(unsigned long long) * ssd->parameter->channel_number);
				for(int ii = 0; ii < ssd->parameter->channel_number; ++ii){
					update->completeCountR[ii] = ssd->channel_head[ii].completeCountR;
					update->completeCountW[ii] = ssd->channel_head[ii].completeCountW;
				}
				if(1){
				//if(ssd->parameter->dynamic_allocation != 2){
					++ssd->chanSubRLenNow[location->channel];
					//ssd->chanSubLenAll[location->channel] += (ssd->chanSubRLenNow[location->channel] + ssd->chanSubWLenNow[location->channel]);
					ssd->chanSubLenAll[location->channel] += (ssd->chanSubRLenNow[location->channel]);
					++ssd->chanSubCount[location->channel];
					if (ssd->channel_head[location->channel].subs_r_tail!=NULL)            /*�����µĶ����󣬲��ҹҵ�channel��subs_r_tail����β*/
					{
						ssd->channel_head[location->channel].subs_r_tail->next_node=update;
						ssd->channel_head[location->channel].subs_r_tail=update;
					} 
					else
					{
						ssd->channel_head[location->channel].subs_r_tail=update;
						ssd->channel_head[location->channel].subs_r_head=update;
					}
				}
				if(preUpdate == NULL){
					sub->update = update;
				}else{
					preUpdate->update = update;
				}
				preUpdate = update;

				location = find_location(ssd, ssd->dram->map->map_entry[lpn].oldppa);
				invaild_semi_page(ssd, location);
				free(location);
				ssd->dram->map->map_entry[lpn].oldppa = -1;
			}
		}
		if(ssd->trip2Page[sub->raidNUM].location){
			unsigned int mask = FULL_PAGE_STATE(ssd->parameter->subpage_page);
			update = creat_sub_request(ssd, sub->lpn, size(mask), mask, NULL,  READ, 0, sub->raidNUM);
			if(preUpdate == NULL){
				sub->update = update;
			}else{
				preUpdate->update = update;
			}
			preUpdate = update;
		}
	}
}

void gc_eject(struct ssd_info* ssd, unsigned long long raidid){
	struct buffer_group *buffer_node = 0, key;
	unsigned int mask = FULL_PAGE_STATE(ssd->parameter->subpage_page);
	if(raidid == 9154){
		printf("gc_eject %lld\n", raidid);
	}
	key.group = raidid;
	ssd->gc_evction++;
	if((buffer_node = (struct buffer_group*)hash_find(ssd->dram->buffer, (HASH_NODE *)&key)) != NULL){
		if(ssd->dram->buffer->buffer_tail == buffer_node){				
			ssd->dram->buffer->buffer_tail = buffer_node->LRU_link_pre;
			
			if(ssd->dram->buffer->buffer_head == buffer_node){
				ssd->dram->buffer->buffer_head = NULL;
			}else{
				buffer_node->LRU_link_pre->LRU_link_next = NULL;
			}				
		}else if(ssd->dram->buffer->buffer_head == buffer_node){
			ssd->dram->buffer->buffer_head = buffer_node->LRU_link_next;
			ssd->dram->buffer->buffer_head->LRU_link_pre = NULL;
		}else{					
			buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;				
			buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;
		}
		

		unsigned int sub_req_state=0, sub_req_size=0,sub_req_lpn=0;
		struct sub_request *sub = NULL;

		sub_req_state = buffer_node->stored;
		sub_req_size = size(sub_req_state);
		sub_req_lpn = buffer_node->group;

		int  calculate = 0;
		for(int i = 0; i < ssd->stripe.all; ++i){
			if(buffer_node->accessCount[i] != 0){
				calculate++;
			}
		}
		// printf("%d\n", calculate);
		ssd->changeCount[calculate - 1]++;

		ssd->allCacheCount += ssd->cacheCountNow;
		ssd->eCount++;
		for(int i = 0; i < ssd->stripe.all - 1; i++){
			ssd->cacheCountNow -= buffer_node->accessCount[i];
		}

		sub = creat_sub_request(ssd,ssd->stripe.checkLpn, sub_req_size, sub_req_state, ssd->raidReq, WRITE, 0, sub_req_lpn);
		if(ssd->channel_head[sub->location->channel].subs_w_head != sub){
			delete_w_sub_request(ssd, sub->location->channel, sub);
			sub->next_node = ssd->channel_head[sub->location->channel].subs_w_head;
			ssd->channel_head[sub->location->channel].subs_w_head = sub;
		}

		ssd->dram->buffer->buffer_sector_count=ssd->dram->buffer->buffer_sector_count - (size(mask));
		hash_del(ssd->dram->buffer, (HASH_NODE *)buffer_node);
		hash_node_free(ssd->dram->buffer, (HASH_NODE *) buffer_node);
		ssd->parityChange++;
		if(sub_req_lpn == 9154){
			printf("eject,22221111,,%d\n", sub_req_lpn);
			for(int i = 0; i < ssd->stripe.all - 1; ++i){
				unsigned long long lpn = ssd->trip2Page[sub_req_lpn].lpn[i];
			
				printf("%lld ppa %lld\n",lpn ,ssd->dram->map->map_entry[lpn].oldppa);
				
			}
			// abort();
		}

		creat_update_sub(ssd, sub);

		if(sub_req_lpn == 9154){
			printf("eject,2222,,%d\n", sub_req_lpn);
		}
		
	}else{
		abort();
	}
}

void cache_fail(struct ssd_info* ssd, struct buffer_group *buffer_node){
	int free_sector=ssd->dram->buffer->max_buffer_sector-ssd->dram->buffer->buffer_sector_count;
	if(free_sector != 0){
		return;
	}
	struct buffer_group *tail = ssd->dram->buffer->buffer_tail;
	for(int i = 0; i < ssd->stripe.all - 1; ++i){
		int j = 0;
		if(buffer_node->accessCount[i] == 0){
			continue;
		}
		if(buffer_node->accessTime[i] == ssd->cacheCount){
			continue;
		}
		ssd->cacheFailAll++;
		for( ; j < ssd->stripe.all - 1; ++j){
			if(tail->accessCount[i] == 0){
				continue;
			}
			if(buffer_node->accessTime[i] < tail->accessTime[j]){
				ssd->cacheFail++;
				break;
			}
		}
	}
}


unsigned long long get_abs(unsigned long long a, unsigned long long b){
	return a > b ? a - b : b - a;
}

void movtiavate(struct ssd_info* ssd, struct buffer_group *buffer_node){
	unsigned long long min = buffer_node->accessTime[0];
	unsigned long long max = buffer_node->accessTime[0];
	int index[ssd->stripe.all - 1];	

	for(int i = 0; i < ssd->stripe.all - 1; ++i){
		index[i] = i;
	}
	for(int i = 0; i < ssd->stripe.all - 1; ++i){
		int max = i;
		for(int j = i + 1; j < ssd->stripe.all - 1; ++j){
			if(buffer_node->accessTime[index[j]] > buffer_node->accessTime[index[max]]){
				max = j;
			}
		}
		if(max != i){
			int tmp = index[i];
			index[i] = index[max];
			index[max] = tmp;
		}
	}

	int diff = 0;
	int i = 1;
	for(int i = 1; i < ssd->stripe.all - 2; ++i){
		if(buffer_node->accessTime[index[diff]] - buffer_node->accessTime[index[diff + 1]] < buffer_node->accessTime[index[i]] - buffer_node->accessTime[index[i + 1]]){
			diff = i;
		}
	}
	unsigned long long d = buffer_node->accessTime[index[diff]] - buffer_node->accessTime[index[diff + 1]];
	if(buffer_node->accessTime[index[diff + 1]] == 0){
		d -= buffer_node->createTime;
	}

	ssd->distanceMoti += d;
	ssd->distanceCountMoti++;

	// for(int i = 0; i < ssd->stripe.all - 1; i++){
	// 	for(int j = i + 1; j < ssd->stripe.all - 1; j++){
	// 		ssd->distanceMoti += get_abs(buffer_node->accessTime[i] != 0 ? buffer_node->accessTime[i] :buffer_node->createTime , buffer_node->accessTime[j]!= 0 ? buffer_node->accessTime[j] : buffer_node->createTime );
	// 		ssd->distanceCountMoti++;
	// 	}
	// 	if(buffer_node->accessTime[i] > max){
	// 		max = buffer_node->accessTime[i];
	// 	}
	// 	if(buffer_node->accessTime[i] && buffer_node->accessTime[i] < min){
	// 		min = buffer_node->accessTime[i];
	// 	}else if(buffer_node->accessTime[i] == 0){
	// 		min = buffer_node->createTime;
	// 	}
		
	// 	if(buffer_node->accessTime[i] == 0){
	// 		continue;
	// 	}
	// }

	ssd->countMoti++;
	ssd->min += ssd->cacheCount - min;
	ssd->max += ssd->cacheCount - max;
}


void ppc_cache(struct ssd_info* ssd, int lpn, unsigned int state, struct request* req, unsigned int mask, unsigned long long userlpn, struct sub_request *presub){
	struct buffer_group *buffer_node = 0, key;
	int i;
	unsigned long long sector_count = size(mask), free_sector; 
	key.group = lpn;
	ssd->cacheCount++;
	if(lpn == -1){
		printf(" raid %d\n", lpn);
		abort();
	}
	if((buffer_node = (struct buffer_group*)hash_find(ssd->dram->buffer, (HASH_NODE *)&key)) != NULL){
		if(ssd->dram->buffer->buffer_head != buffer_node){
			movtiavate(ssd, buffer_node);
			move_to_head(ssd, buffer_node);
		}
		buffer_node->stored |= state;
		ssd->cacheHit++;
	}else{
		unsigned char flag = 0;
		free_sector=ssd->dram->buffer->max_buffer_sector-ssd->dram->buffer->buffer_sector_count;  
		
		if(free_sector>=sector_count){
			flag=1;    
		}
		
		if(flag==0){
			ssd->cachedoNotHit++;
			unsigned long long  write_back_count=sector_count - free_sector;
			unsigned int sub_req_state=0, sub_req_size=0,sub_req_lpn=0;
			struct sub_request *sub = NULL;
			buffer_node = get_last(ssd);
			ssd->parityChange++;
			sub_req_state = buffer_node->stored;
			sub_req_size = size(sub_req_state);
			sub_req_lpn = buffer_node->group;

			sub = creat_sub_request(ssd,ssd->stripe.checkLpn, sub_req_size, sub_req_state, req, WRITE, 0, sub_req_lpn);
			
			ssd->allCacheCount += ssd->cacheCountNow;
			ssd->eCount++;
			for(int i = 0; i < ssd->stripe.all - 1; i++){
				ssd->cacheCountNow -= buffer_node->accessCount[i];
			}

			int  calculate = 0;
			for(i = 0; i < ssd->stripe.all; ++i){
				if(buffer_node->accessCount[i] != 0){
					calculate++;
				}
			}
			// printf("%d\n", calculate);
			ssd->changeCount[calculate - 1]++;

			ssd->dram->buffer->buffer_sector_count=ssd->dram->buffer->buffer_sector_count - (size(mask));
			hash_del(ssd->dram->buffer, (HASH_NODE *)buffer_node);
			hash_node_free(ssd->dram->buffer, (HASH_NODE *) buffer_node);

			// creat_update_sub(ssd, sub);
			

			if(sub_req_lpn == 9154)
				printf("eject,2,,%d\n", sub_req_lpn);
		}else{
			ssd->cacheHit++;
		}

		if(lpn == 9154)
			printf("insert,2,,%d\n", lpn);
		buffer_node = (struct buffer_group *)malloc(sizeof(struct buffer_group));
		alloc_assert(buffer_node,"buffer_group_node");
		memset(buffer_node,0, sizeof(struct buffer_group));

		buffer_node->accessCount = malloc(sizeof(unsigned int) * ssd->stripe.all);
		alloc_assert(buffer_node->accessCount,"buffer_node->accessCount");
		memset(buffer_node->accessCount, 0, sizeof(unsigned int) * ssd->stripe.all);

		buffer_node->accessTime = malloc(sizeof(unsigned long long) * ssd->stripe.all);
		alloc_assert(buffer_node->accessTime,"buffer_node->accessCount");
		memset(buffer_node->accessTime, 0, sizeof(unsigned long long) * ssd->stripe.all);

		buffer_node->read_count = 0;
		buffer_node->group=lpn;
		buffer_node->stored=state;
		buffer_node->dirty_clean=state;
		buffer_node->createTime = ssd->cacheCount;
		buffer_node->LRU_link_pre = NULL;
		buffer_node->LRU_link_next=ssd->dram->buffer->buffer_head;
		if(ssd->dram->buffer->buffer_head != NULL){
			ssd->dram->buffer->buffer_head->LRU_link_pre=buffer_node;
		}else{
			ssd->dram->buffer->buffer_tail = buffer_node;
		}
		ssd->dram->buffer->buffer_head=buffer_node;
		buffer_node->LRU_link_pre=NULL;
		//avlTreeAdd(ssd->dram->buffer, (TREE_NODE *) new_node);
		hash_add(ssd->dram->buffer, (HASH_NODE *) buffer_node);
		ssd->dram->buffer->buffer_sector_count += sector_count;

		if(req && ssd->trip2Page[buffer_node->group].location){
			creat_sub_request(ssd, ssd->stripe.checkLpn, size(ssd->dram->map->map_entry[userlpn].state), ssd->dram->map->map_entry[userlpn].state, req, READ, 0, buffer_node->group);
		}
	}
	
	for(i = 0; i < ssd->stripe.all; ++i){
		if(ssd->trip2Page[buffer_node->group].lpn[i] == userlpn){
			buffer_node->accessCount[i]++;
			buffer_node->accessTime[i] = ssd->cacheCount;
			ssd->cacheCountNow++;
			break;
		}
	}
	
	if(i == ssd->stripe.all){
		for(int i = 0; i < ssd->stripe.all; ++i){
			printf("%lld\n", ssd->trip2Page[buffer_node->group].lpn[i]);
		}
		printf("check %llu\n", userlpn);
		abort();
	}

	if(ssd->dram->map->map_entry[userlpn].state != 0 && req && presub && presub->update == NULL){
		creat_sub_request(ssd, userlpn, size(ssd->dram->map->map_entry[userlpn].state), ssd->dram->map->map_entry[userlpn].state, req, READ, 0, ssd->page2Trip[userlpn]);
	}
	cache_fail(ssd, buffer_node);
	if(ssd->trip2Page[buffer_node->group].location){
		invaild_page(ssd, ssd->trip2Page[buffer_node->group].location);
		free(ssd->trip2Page[buffer_node->group].location);
		ssd->trip2Page[buffer_node->group].location = NULL;
	}
}

void creat_sub_write_request_for_raid(struct ssd_info* ssd, int lpn, unsigned int state, struct request* req, unsigned int mask){
	unsigned int i, j;
	struct request *nowreq = ssd->request_queue;
	struct sub_request *sub, *psub;
	int channel = -1;
	int raidid = 0;
	
	while(nowreq && channel == -1){
		if(nowreq->operation == WRITE){
			sub	= nowreq->subs;
			while(sub && channel == -1){
				if(sub->lpn == lpn && sub->current_state==SR_WAIT){
					sub->state |= state;
					sub->size = size(sub->state);
					req->all--;
					req->MergeFlag = 1;
					channel = sub->location->channel;
					raidid = sub->raidNUM;
					//return;
				}
				sub = sub->next_subs;
			}
		}
		nowreq = nowreq->next_node;
	}
	sub	= ssd->raidReq->subs;
	while(sub && channel == -1){
		if(sub->lpn == lpn && sub->current_state==SR_WAIT){
			sub->state |= state;
			sub->size = size(sub->state);
			req->all--;
			req->MergeFlag = 1;
			channel = sub->location->channel;
			raidid = sub->raidNUM;
			//return;
		}
		sub = sub->next_subs;
	}

	if(channel != -1){
		sub = create_recovery_sub_write(ssd, channel, lpn, req);
		sub->raidNUM = raidid;
		return;
	}
	
	if(ssd->stripe.now == 0){
		ssd->stripe.now = 0;
		ssd->trip2Page[ssd->stripe.nowStripe].using = 1;
		// printf("%d\n", ssd->ssdToken);
		while(ssd->trip2Page[ssd->stripe.nowStripe].using || ssd->stripe.nowStripe == 0 || ssd->stripe.nowStripe == ssd->stripe.allStripe){
			if(++ssd->stripe.nowStripe == ssd->stripe.allStripe || ssd->stripe.nowStripe == 0)
				ssd->stripe.nowStripe = 1;
		}
		ssd->trip2Page[ssd->stripe.nowStripe].Pchannel = 3 * ssd->perChanSSD + ssd->channelToken;
		if(ssd->trip2Page[ssd->stripe.nowStripe].Pchannel == 19){
			printf("%llu %d %d\n", ssd->ssdToken,  ssd->channelToken, ssd->stripe.check);
			abort();
		}
		ssd->paritycount[ssd->trip2Page[ssd->stripe.nowStripe].Pchannel]++;
		if((((ssd->trip2Page[ssd->stripe.nowStripe].Pchannel / (ssd->stripe.all * ssd->perChanSSD)) + 1) * (ssd->stripe.all * ssd->perChanSSD) == ssd->parameter->channel_number) && (ssd->channelToken == (ssd->perChanSSD - 1)) ){
			ssd->stripe.check = 3;
		}
	}

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
	
	sub = creat_sub_request(ssd, lpn, size(state), state, req, req->operation, TARGET_LSB, ssd->stripe.nowStripe);
	// printf("%d %d\n", ssd->ssdToken, ssd->stripe.now);
	ssd->trip2Page[ssd->stripe.nowStripe].lpn[ssd->stripe.now] = lpn;
	ssd->page2Trip[lpn] = ssd->stripe.nowStripe;
	
	ppc_cache(ssd, ssd->page2Trip[lpn], state, req, mask, lpn, sub);
	if(ssd->stripe.nowStripe == 9154){
		printf("9154 11111 %d\n", lpn);
	}
	
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

	if(++ssd->stripe.now == (ssd->stripe.all - 1)){
		ssd->stripe.now = 0;
	}
	// if(ssd->channelToken == 0){
	// 	if(++ssd->stripe.check >= (ssd->stripe.all))
	// 		ssd->stripe.check = 3;
	// }

}

void create_parity(struct ssd_info *ssd, struct request *req,long long raidID){
	int changeNUM = 0;
	unsigned int mask=0;
	struct sub_request *sub, *XOR_req;

	if(ssd->parameter->subpage_page == 32){
		mask = 0xffffffff;
	}else{
		mask=FULL_PAGE_STATE(ssd->parameter->subpage_page);
	}
	if(ssd->trip2Page[raidID].Pchannel != -1){
		for(int i = 0; i < ssd->stripe.all - 1; ++i){
			int lpn = ssd->trip2Page[raidID].lpn[i];
			unsigned long long offset = lpn % 8;
			unsigned long long pos = lpn / 8;
			if(ssd->raidBitMap[pos] & (1 << offset)){
				++changeNUM;
			}
		}
		if(changeNUM == 0){
			return;
		}
		if(ssd->trip2Page[raidID].location && changeNUM <= (ssd->stripe.all - 1) / 2){
			int lpn = ssd->stripe.checkLpn;
			sub = creat_sub_request(ssd, lpn, size(mask), mask, \
					req, READ, 0, raidID);
		}else{
			for(int i = 0; i < ssd->stripe.all - 1; ++i){
				int lpn = ssd->trip2Page[raidID].lpn[i];
				unsigned long long offset = lpn % 8;
				unsigned long long pos = lpn / 8;
				if(ssd->raidBitMap[pos] & (1 << offset)){
					if(ssd->dram->map->map_entry[lpn].state)
						sub = creat_sub_request(ssd, lpn, size(ssd->dram->map->map_entry[lpn].state), ssd->dram->map->map_entry[lpn].state, \
						req, READ, 0, raidID);
					
				}
			}
		}
		XOR_req = creat_sub_request(ssd, ssd->stripe.checkLpn , size(mask), mask,\
						req, WRITE, TARGET_LSB, raidID);
		
		XOR_process(ssd, 16);
	}
	
	for(int i = 0; i < ssd->stripe.all - 1; ++i){
		int lpn = ssd->trip2Page[raidID].lpn[i];
		unsigned long long offset = lpn % 8;
		unsigned long long pos = lpn / 8;
		ssd->raidBitMap[pos] &= (unsigned char)~(1u << offset);
	}
}


struct ssd_info *no_buffer_distribute(struct ssd_info *ssd)
{
	unsigned int lsn,lpn,last_lpn,first_lpn,complete_flag=0, state;
	unsigned int flag=0,flag1=1,active_region_flag=0;           //to indicate the lsn is hitted or not
	struct request *req=NULL;
	struct sub_request *sub=NULL,*sub_r=NULL,*update=NULL;
	struct local *loc=NULL;
	struct channel_info *p_ch=NULL;
	int i;
	
	unsigned int mask=0; 
	unsigned int offset1=0, offset2=0;
	unsigned int sub_size=0;
	unsigned int sub_state=0;
	if(ssd->cpu_current_state == CPU_BUSY && ssd->cpu_next_state_predict_time > ssd->current_time)
		return ssd;
	
		
	ssd->dram->current_time=ssd->current_time;
	req=ssd->request_tail;       
	lsn=req->lsn;
	lpn=req->lsn/ssd->parameter->subpage_page;
	last_lpn=(req->lsn+req->size-1)/ssd->parameter->subpage_page;
	first_lpn=req->lsn/ssd->parameter->subpage_page;

	if(req->operation==READ)        
	{		
		while(lpn<=last_lpn) 		
		{
			int nextLpn = lpn;
			lpn = lpn % ssd->stripe.checkStart;
			lpn += 1;
			unsigned off = nextLpn == first_lpn ? req->lsn % ssd->parameter->subpage_page : 0;
            unsigned n = ssd->parameter->subpage_page - off;
            if (nextLpn == last_lpn) n = (req->lsn + req->size - 1) % ssd->parameter->subpage_page + 1 - off;
            sub_state = ssd->dram->map->map_entry[lpn].state & (unsigned)set_entry_state(ssd, off, n);
			sub_size=size(sub_state);
			sub=creat_sub_request(ssd,lpn,sub_size,sub_state,req,req->operation,0, ssd->page2Trip[lpn]);
			// lpn++;
			lpn = ++nextLpn;
		}
	}
	else if(req->operation==WRITE)
	{
		int target_page_type;
		int random_num;
		random_num = rand()%100;
		if(random_num<ssd->parameter->turbo_mode_factor){
			target_page_type = TARGET_LSB;
			}
		else if(random_num<ssd->parameter->turbo_mode_factor_2){
			target_page_type = TARGET_CSB;
			}
		else{
			target_page_type = TARGET_MSB;
			}
		while(lpn<=last_lpn)     	
		{	
			int nextLpn = lpn;
			lpn = lpn % ssd->stripe.checkStart;
			lpn += 1;
			if(ssd->parameter->subpage_page == 32){
				mask = 0xffffffff;
				}
			else{
				mask=FULL_PAGE_STATE(ssd->parameter->subpage_page);
				}
			state=mask;
			//printf("initial state: %x\n", state);
			if(nextLpn==first_lpn)
			{
				offset1=ssd->parameter->subpage_page-((nextLpn+1)*ssd->parameter->subpage_page-req->lsn);
				//printf("offset1: %d, ", offset1);
				state=state&(0xffffffff<<offset1);
				//printf("state: %x\n", state);
			}
			if(nextLpn==last_lpn)
			{
				offset2=ssd->parameter->subpage_page-((nextLpn+1)*ssd->parameter->subpage_page-(req->lsn+req->size));
				//printf("offset2: %d, ", offset2);
				if(offset2 != 32){
					state=state&(~(0xffffffff<<offset2));
					}
				//printf("state: %x\n", state);
			}
			//printf("state: %x, ", state);
			req->all++;
			//printf("1111\n");
			fprintf(ssd->footPoint,"%u\n",lpn);
			if(ssd->dram->map->map_entry[lpn].state == 0){
				creat_sub_write_request_for_raid(ssd,lpn, state, req, mask);
			}else {
				sub_size=size(state);

				sub=creat_sub_request(ssd,lpn,sub_size,state,req,req->operation,target_page_type, ssd->page2Trip[lpn]);
				ppc_cache(ssd, ssd->page2Trip[lpn], state, req, mask, lpn, sub);
			}
			// lpn++;
			lpn = ++nextLpn;
		}
	}
	/* CPU timing is scheduled by XOR_process; do not overwrite it here. */
	return ssd;
}

