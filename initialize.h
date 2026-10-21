/*****************************************************************************************************************************
This project was supported by the National Basic Research 973 Program of China under Grant No.2011CB302301
Huazhong University of Science and Technology (HUST)   Wuhan National Laboratory for Optoelectronics

FileName�� initialize.h
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
#ifndef INITIALIZE_H
#define INITIALIZE_H
#include <stdint.h>
#define FULL_PAGE_STATE(n) ((n) == 32 ? UINT32_MAX : ((1u << (n)) - 1u)) 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <sys/types.h>
//#include "avlTree.h"


typedef struct _HASH_NODE_
{
	struct _HASH_NODE_ *next;
}HASH_NODE;

#define SECTOR 512
#define BUFSIZE 200

#define DYNAMIC_ALLOCATION 0
#define STATIC_ALLOCATION 1

#define INTERLEAVE 0
#define TWO_PLANE 1

#define NORMAL    2
#define INTERLEAVE_TWO_PLANE 3
#define COPY_BACK	4

#define AD_RANDOM 1
#define AD_COPYBACK 2
#define AD_TWOPLANE 4
#define AD_INTERLEAVE 8
#define AD_TWOPLANE_READ 16

#define READ 1
#define WRITE 0

#define WL_THRESHOLD 1.1
#define BRT 38000

/*********************************all states of each objects************************************************
*һ�¶�����channel�Ŀ��У������ַ���䣬���ݴ��䣬���䣬������״̬
*����chip�Ŀ��У�дæ����æ�������ַ���䣬���ݴ��䣬����æ��copybackæ��������״̬
*���ж�д������sub���ĵȴ����������ַ���䣬���������ݴ��䣬д�����ַ���䣬д���ݴ��䣬д���䣬��ɵ�״̬
************************************************************************************************************/

#define CHANNEL_IDLE 000
#define CHANNEL_C_A_TRANSFER 3
#define CHANNEL_GC 4           
#define CHANNEL_DATA_TRANSFER 7
#define CHANNEL_TRANSFER 8
#define CHANNEL_UNKNOWN 9

#define CHIP_IDLE 100
#define CHIP_WRITE_BUSY 101
#define CHIP_READ_BUSY 102
#define CHIP_C_A_TRANSFER 103
#define CHIP_DATA_TRANSFER 107
#define CHIP_WAIT 108
#define CHIP_ERASE_BUSY 109
#define CHIP_COPYBACK_BUSY 110
#define UNKNOWN 111

#define SR_WAIT 200                 
#define SR_R_C_A_TRANSFER 201
#define SR_R_READ 202
#define SR_R_DATA_TRANSFER 203
#define SR_W_C_A_TRANSFER 204
#define SR_W_DATA_TRANSFER 205
#define SR_W_TRANSFER 206
#define SR_COMPLETE 299

#define CPU_IDLE 700
#define CPU_BUSY 701

#define REQUEST_IN 300         //��һ�����󵽴��ʱ��
#define OUTPUT 301             //��һ�����������ʱ��
#define GC_WAIT 400
#define GC_ERASE_C_A 401
#define GC_COPY_BACK 402
#define GC_COMPLETE 403
#define GC_INTERRUPT 0
#define GC_UNINTERRUPT 1
#define GC_FAST_UNINTERRUPT 1700
#define GC_FAST_UNINTERRUPT_EMERGENCY 1701
#define GC_FAST_UNINTERRUPT_IDLE 1702
#define DR_STATE_RECIEVE 1800
#define DR_STATE_OUTPUT 1801
#define DR_STATE_NULL 1802

#define CHANNEL(lsn) (lsn&0x0000)>>16      
#define chip(lsn) (lsn&0x0000)>>16 
#define die(lsn) (lsn&0x0000)>>16 
#define PLANE(lsn) (lsn&0x0000)>>16 
#define BLOKC(lsn) (lsn&0x0000)>>16 
#define PAGE(lsn) (lsn&0x0000)>>16 
#define SUBPAGE(lsn) (lsn&0x0000)>>16  

#define PG_SUB 0xffffffff			

/*****************************************
*�������״̬����
*Status �Ǻ������ͣ���ֵ�Ǻ������״̬����
******************************************/
#define TRUE		1
#define FALSE		0
#define SUCCESS		1
#define FAILURE		0
#define ERROR		-1
#define INFEASIBLE	-2
#define OVERFLOW	-3

//**************************
#define SUCCESS_LSB 1
#define SUCCESS_MSB 2
#define SUCCESS_CSB 3
#define TARGET_LSB 0
#define TARGET_CSB 1
#define TARGET_MSB 2
//**************************

typedef int Status;     

struct ac_time_characteristics{
	int tPROG;     //program time
	int tDBSY;     //bummy busy time for two-plane program
	int tBERS;     //block erase time
	int tCLS;      //CLE setup time
	int tCLH;      //CLE hold time
	int tCS;       //CE setup time
	int tCH;       //CE hold time
	int tWP;       //WE pulse width
	int tALS;      //ALE setup time
	int tALH;      //ALE hold time
	int tDS;       //data setup time
	int tDH;       //data hold time
	int tWC;       //write cycle time
	int tWH;       //WE high hold time
	int tADL;      //address to data loading time
	int tR;        //data transfer from cell to register
	int tAR;       //ALE to RE delay
	int tCLR;      //CLE to RE delay
	int tRR;       //ready to RE low
	int tRP;       //RE pulse width
	int tWB;       //WE high to busy
	int tRC;       //read cycle time
	int tREA;      //RE access time
	int tCEA;      //CE access time
	int tRHZ;      //RE high to output hi-z
	int tCHZ;      //CE high to output hi-z
	int tRHOH;     //RE high to output hold
	int tRLOH;     //RE low to output hold
	int tCOH;      //CE high to output hold
	int tREH;      //RE high to output time
	int tIR;       //output hi-z to RE low
	int tRHW;      //RE high to WE low
	int tWHR;      //WE high to RE low
	int tRST;      //device resetting time
	int tPROG_L;   //program time for LSB page
	int tPROG_C;   //program time for CSB page
	int tPROG_M;   //program time for MSB page
	int t_R_L;   //READ time for LSB page
	int t_R_L1;
	int t_R_L2;
	int t_R_L3;
	int t_R_C;   //READ time for CSB page
	int t_R_C1;
	int t_R_C2;
	int t_R_C3;
	int t_R_M;   //READ time for MSB page
	int t_R_M1;
	int t_R_M2;
	int t_R_M3;
};

struct StripeReq{
	unsigned int lpn;
	unsigned int state;
	unsigned int sub_size;
	unsigned int full_page;
	struct request *req;
	int lpnCount;
};

struct Stripe{
	unsigned int check;
	unsigned int all;
	unsigned int now;
	unsigned int checkLpn;
	unsigned int checkStart;
	unsigned long long allStripe;
	unsigned long long nowStripe;
	struct StripeReq *req;
};	

enum{
	INVAIL_DRAID = 0,
	VAIL_DRAID = 1,
	CHECK_RAID = 2,
	WILLCHANGE_RAID = 3,
};

struct RaidInfo{
    unsigned int parity_state;
	long long* lpn;
	unsigned int changeState;
	unsigned int* check;
	unsigned char allChange;
	unsigned char nowChange;
	unsigned char using;
	unsigned char readFlag;
	struct local* location;
	int Pchannel;
};

struct raid4_controller;
struct ssd_info{
    struct raid4_controller *raid4; 
	double ssd_energy;                   //SSD���ܺģ���ʱ���оƬ���ĺ���,�ܺ�����
	int64_t current_time;                //��¼ϵͳʱ��
	int64_t next_request_time;
	unsigned int real_time_subreq;       //��¼ʵʱ��д�������������ȫ��̬����ʱ��channel���ȵ����
	int flag;
	int active_flag;                     //��¼����д�Ƿ����������������������Ҫ��ʱ����ǰ�ƽ�,0��ʾû��������1��ʾ����������Ҫ��ǰ�ƽ�ʱ��
	unsigned int page;

	unsigned int token;                  //�ڶ�̬�����У�Ϊ��ֹÿ�η����ڵ�һ��channel��Ҫά��һ�����ƣ�ÿ�δ�������ָ��λ�ÿ�ʼ����
	unsigned int gc_request;             //��¼��SSD�У���ǰʱ���ж���gc����������

	unsigned int write_request_count;    //��¼д�����Ĵ���
	unsigned int read_request_count;     //��¼�������Ĵ���
	unsigned long long write_avg;                   //��¼���ڼ���д����ƽ����Ӧʱ���ʱ��
	unsigned long long read_avg;                    //��¼���ڼ��������ƽ����Ӧʱ���ʱ��
	//================================================
	int time_day;
	unsigned int performance_mode;
	unsigned int max_queue_depth;
	unsigned int write_request_count_l;
	unsigned int newest_write_request_count;
	unsigned int newest_read_request_count;
	int64_t newest_write_avg;
	int64_t write_avg_l;
	int64_t newest_write_avg_l;
	int64_t newest_read_avg;
	unsigned long completed_request_count;
	unsigned long total_request_num;
	unsigned int moved_page_count;
	unsigned int idle_fast_gc_count;
	unsigned int last_write_lat;
	unsigned int last_ten_write_lat[10];
	unsigned int write_lat_anchor;

	unsigned int newest_write_request_completed_with_same_type_pages;
	unsigned int newest_req_with_msb;
	unsigned int newest_req_with_csb;
	unsigned int newest_req_with_lsb;

	unsigned int newest_write_request_num_l;
	unsigned int newest_write_request_completed_with_same_type_pages_l;
	unsigned int newest_req_with_msb_l;
	unsigned int newest_req_with_csb_l;
	unsigned int newest_req_with_lsb_l;
	
	unsigned int newest_lsb_request_a;
	unsigned int newest_msb_request_a;
	unsigned int newest_csb_request_a;
	int speed_up;
	//================================================

	unsigned int min_lsn;
	unsigned int max_lsn;
	unsigned long read_count;
	unsigned long program_count;
	unsigned long erase_count;
	unsigned long direct_erase_count;
	unsigned long copy_back_count;
	unsigned long m_plane_read_count;
	unsigned long m_plane_prog_count;
	unsigned long interleave_count;
	unsigned long interleave_read_count;
	unsigned long inter_mplane_count;
	unsigned long inter_mplane_prog_count;
	unsigned long interleave_erase_count;
	unsigned long mplane_erase_conut;
	unsigned long interleave_mplane_erase_count;
	unsigned long gc_copy_back;
	unsigned long write_flash_count;     //ʵ�ʲ����Ķ�flash��д����
	unsigned long sub_request_success;
	unsigned long sub_request_all;
	//=============================================================
	unsigned long write_lsb_count;
	unsigned long write_msb_count;
	unsigned long write_csb_count;
	unsigned long newest_write_lsb_count;
	unsigned long newest_write_msb_count;
	unsigned long newest_write_csb_count;
	unsigned long fast_gc_count;
	int64_t basic_time;
	unsigned long free_lsb_count;
	unsigned long free_csb_count;
	unsigned long free_msb_count;
	//=============================================================
	
	unsigned long waste_page_count;      //��¼��Ϊ�߼���������Ƶ��µ�ҳ�˷�
	float ave_read_size;
	float ave_write_size;
	unsigned int request_queue_length;
	unsigned int update_read_count;      //��¼��Ϊ���²������µĶ����������

	char parameterfilename[30];
	char tracefilename[1024];
	char outputfilename[2048];
	char statisticfilename[2048];
	char statisticfilename2[30];

	FILE * raidOutfile;
	FILE * gcCreateRequest;
	FILE * gcIntervalFile;
	FILE * outputfile;
	FILE * tracefile;
	FILE * statisticfile;
	FILE * statisticfile2;
	FILE * motivateFile;

    struct parameter_value *parameter;   //SSD��������
	struct dram_info *dram;
	struct request *request_queue;       //dynamic request queue
	struct request *request_tail;	     // the tail of the request queue
	struct sub_request *subs_w_head;     //������ȫ��̬����ʱ�������ǲ�֪��Ӧ�ù����ĸ�channel�ϣ������ȹ���ssd�ϣ��Ƚ���process����ʱ�Źҵ���Ӧ��channel�Ķ����������
	struct sub_request *subs_w_tail;
	struct event_node *event;            //�¼����У�ÿ����һ���µ��¼�������ʱ��˳��ӵ�������У���simulate������󣬸���������ж��׵�ʱ�䣬ȷ��ʱ��
	struct channel_info *channel_head;   //ָ��channel�ṹ��������׵�ַ

	struct block_erase_count* block_head;//������ָ���������
	unsigned int page_move_count;        //page move in gc
	unsigned int page_move_count_1;        //page move in gc
	unsigned int page_move_count_erase;        //page move in gc
	unsigned int wl_page_move_count;     //page move in wl
	unsigned int rr_page_move_count;
	float mapping_time;
	float avg_erase;                     //��¼��������ƽ��ֵ
	unsigned int max_erase;              //��¼��ǰ�������ֵ
	int wl_hotdata_flag;                 //��Ǵ�ʱ�Ƿ���wl�е�hotdata
	int wl_colddata_flag;                 //��Ǵ�ʱ�Ƿ���wl�е�colddata
	int wl_hotdata_count;                //wl��������е�hot read data�ļ���
	int wl_colddata_count;               //wl��������е�cold read data�ļ���
	int find_a_empty_block;              //flag��¼�Ƿ�Ҫ��һ��empty�Ŀ�
	unsigned int wl_count;
	unsigned int wl_delta;
	//struct bitmap *bit;                  //λ������¼�����Щ���Ƿ��޸Ĺ�
	struct block_update_table* block_update;//��¼���Ƿ�updated
	struct block_update_table* block_update_current;//��¼��ǰ��λ��
	float table_count;                      //��¼����flagΪ1�ĸ���f count
	float total_erase_count;              //��¼��ǰ��erase����,e count
	int bet_num;                          //f index
	int BET[65535];                      //Block Erase Table, one array indicates one block status
	//int UBT[32767];
	unsigned int wl_request;
	//int PRT[4718591];                     //Page Read Table,i.e. record top20 blocks' page
	struct wl_table* wl_block_a;          //proposed scheme's block a
	struct wl_table* wl_block_b;          //proposed scheme's block b
	int wl_current_block;                 //��¼��ǰ���ڽ���wl�Ŀ�
	int wl_flag;                          //0��ʾ������gc������1��ʾwl����
	int selected_block_flag;//1��ʾ�˿���hotdataռ�������90%���Ŀ飬0��ʾ�˿���hotdataռ������10%���Ŀ�,2��ʾ�˿��hotdata����10%-90%֮��
	struct wl_table* current_selected_block_a;//��¼��ǰ��ԭtable��ƥ���tableλ��a
	struct wl_table* current_selected_block_b;//��¼��ǰ��ԭtable��ƥ���tableλ��b
	int wl_match_flag;               //1��ʾ��wl�Ŀ������û�б�ƥ�䣬0Ϊ��ʼֵ

	int uninterrupt_gc_count;				//debug
	int move_page_debug;					//debug

	unsigned int init_count;
	int rr_request;
	int rr_erase_count;
	int wl_erase_count;
	int normal_erase;
	int same_block_flag;

	/*add for count invaild pageby winks*/
	unsigned long invaild_page_of_all;
	unsigned long invaild_page_of_change;
	unsigned long frequently_count;
	unsigned long Nofrequently_count;
	unsigned long counttmp1;

	long long *page2Trip;
	struct RaidInfo *trip2Page;
	struct Stripe stripe;
	struct request *raidReq;
	unsigned char *raidBitMap;

	unsigned long long raidWriteCount[2];
	
	unsigned int chipToken;
	unsigned int channelToken;
	
	int cpu_current_state;                  //channel has serveral states, including idle, command/address transfer,data transfer,unknown
	int cpu_next_state;
	int64_t cpu_next_state_predict_time;

	unsigned char *bufferBitMap;
	struct request *readBufReqQue;
	
	unsigned long long read1;
	unsigned long long read2;
	unsigned long long read3;
	unsigned long long read4;
	unsigned long long pageMoveRaid;
	unsigned long long parityChange;

	unsigned long long allPage;
	unsigned long long *chipWrite;
	unsigned long long *chipGc;
	unsigned long long allUsedPage;
	unsigned long long gcInterval128;
	unsigned long long gcInterval256;

	unsigned long long *preRequestArriveTime;
	int preRequestArriveTimeIndex;
	int preRequestArriveTimeIndexMAX;
	unsigned long long degradeCount;


	unsigned long long *chanSubLenAll;
	unsigned long long *chanSubWLenNow;
	unsigned long long *chanSubRLenNow;
	unsigned long long *chanSubCount;

	unsigned long long *chanSubRWriteTime;
	unsigned long long subRWaitCount;

	unsigned long long chanSubRWriteTime_2;
	unsigned long long subRWaitCount_2;

	unsigned long long *preSubWriteLpn;
	int preSubWriteLen;
	int preSubWriteNowPos;

	double temp_read_error_rate;
	double read_error_rate;
	unsigned long long errCount[4];
	unsigned long long errDegradeCount[7];

	unsigned long long firstTime_;

	unsigned long long gc_evction;

	unsigned long long *paritycount;

	unsigned long long recoveryTime;
	unsigned long long needRecoveryAll;
	unsigned long long needRecoveryInvalid;
	unsigned long long *chanRs;

	unsigned long long changeCount[3];

	unsigned long long cacheHit;
	unsigned long long cachedoNotHit;
	unsigned long long cacheCount;

	unsigned long long cacheFail;
	unsigned long long cacheFailAll;

	unsigned long long perChanSSD;
	unsigned long long ssdToken;

	unsigned long long max;
	unsigned long long min;
	unsigned long long allMoti;
	unsigned long long distanceMoti;
	unsigned long long distanceCountMoti;
	unsigned long long countMoti;
	unsigned long long allCacheCount;
	unsigned long long cacheCountNow;
	unsigned long long eCount;

	FILE *footPoint;
};

struct wl_table {
	unsigned int selected_channel;
	unsigned int selected_chip;
	unsigned int selected_die;
	unsigned int selected_plane;
	unsigned int selected_block;
	int selected_block_flag;    //1��ʾ�˿���hotdataռ�������90%���Ŀ飬0��ʾ�˿���hotdataռ������10%���Ŀ�
	unsigned int channel;
	unsigned int chip;
	unsigned int die;
	unsigned int plane;
	unsigned int block;
	struct wl_table* next;
};


struct block_erase_count {
	unsigned int channel;
	unsigned int chip;
	unsigned int die;
	unsigned int plane;
	unsigned int block;
	unsigned int erase_count;
	int update_flag;        //1 update recently, 0 not update recently.
	struct block_erase_count* next;
};

	



struct channel_info{
	int chip;                            //��ʾ�ڸ��������ж��ٿ���
	unsigned long read_count;
	unsigned long program_count;
	unsigned long erase_count;
	unsigned int token;                  //�ڶ�̬�����У�Ϊ��ֹÿ�η����ڵ�һ��chip��Ҫά��һ�����ƣ�ÿ�δ�������ָ��λ�ÿ�ʼ����

	//***************************************
	unsigned long fast_gc_count;
	//***************************************
	int current_state;                   //channel has serveral states, including idle, command/address transfer,data transfer,unknown
	int next_state;
	int64_t current_time;                //��¼��ͨ���ĵ�ǰʱ��
	int64_t next_state_predict_time;     //the predict time of next state, used to decide the sate at the moment

	struct event_node *event;
	struct sub_request *subs_r_head;     //channel�ϵĶ��������ͷ���ȷ����ڶ���ͷ��������
	struct sub_request *subs_r_tail;     //channel�ϵĶ��������β���¼ӽ�����������ӵ���β
	struct sub_request *subs_w_head;     //channel�ϵ�д�������ͷ���ȷ����ڶ���ͷ��������
	struct sub_request *subs_w_tail;     //channel�ϵ�д������У��¼ӽ�����������ӵ���β
	struct gc_operation *gc_command;     //��¼��Ҫ����gc��λ��
	struct chip_info *chip_head;
	/*add by winks*/
	struct gc_operation *last_gc_command;     //��¼��Ҫ����gc��λ��

	unsigned long long completeCountR;
	unsigned long long completeCountW;
	unsigned long long blockCountR;
	unsigned long long blockCountW;
};


struct chip_info{
	unsigned int die_num;               //��ʾһ���������ж��ٸ�die
	unsigned int plane_num_die;         //indicate how many planes in a die
	unsigned int block_num_plane;       //indicate how many blocks in a plane
	unsigned int page_num_block;        //indicate how many pages in a block
	unsigned int subpage_num_page;      //indicate how many subpage in a page
	unsigned int ers_limit;             //��chip��ÿ���ܹ��������Ĵ���
	unsigned int token;                 //�ڶ�̬�����У�Ϊ��ֹÿ�η����ڵ�һ��die��Ҫά��һ�����ƣ�ÿ�δ�������ָ��λ�ÿ�ʼ����
	
	int current_state;                  //channel has serveral states, including idle, command/address transfer,data transfer,unknown
	int next_state;
	int64_t current_time;               //��¼��ͨ���ĵ�ǰʱ��
	int64_t next_state_predict_time;    //the predict time of next state, used to decide the sate at the moment
 
	unsigned long read_count;           //how many read count in the process of workload
	unsigned long program_count;
	unsigned long erase_count;

	//***************************************
	unsigned long fast_gc_count;
	//***************************************

    struct ac_time_characteristics ac_timing;  
	struct die_info *die_head;
};


struct die_info{

	unsigned int token;                 //�ڶ�̬�����У�Ϊ��ֹÿ�η����ڵ�һ��plane��Ҫά��һ�����ƣ�ÿ�δ�������ָ��λ�ÿ�ʼ����
	struct plane_info *plane_head;
	
};


struct plane_info{
	int add_reg_ppn;                    //read��writeʱ�ѵ�ַ���͵��ñ������ñ���������ַ�Ĵ�����die��busy��Ϊidleʱ�������ַ //�п�����Ϊһ�Զ��ӳ�䣬��һ��������ʱ���ж����ͬ��lpn��������Ҫ��ppn������  
	unsigned int free_page;             //��plane���ж���free page
	unsigned int ers_invalid;           //��¼��plane�в���ʧЧ�Ŀ���
	unsigned int active_block;          //if a die has a active block, �����ʾ���������
	//********************************
	unsigned int free_lsb_num;
	unsigned int active_lsb_block;
	unsigned int active_csb_block;
	unsigned int active_msb_block;
	//********************************
	int can_erase_block;                //��¼��һ��plane��׼����gc�����б����������Ŀ�,-1��ʾ��û���ҵ����ʵĿ�
	struct direct_erase *erase_node;    //������¼����ֱ��ɾ���Ŀ��,�ڻ�ȡ�µ�ppnʱ��ÿ������invalid_page_num==64ʱ���������ӵ����ָ���ϣ���GC����ʱֱ��ɾ��
	//*****************************************
	struct direct_erase *fast_erase_node;
	//*****************************************
	struct blk_info *blk_head;
	/*add for flag write frenquentlyactive block*/
	unsigned int active_block_write;          //if a die has a active block, �����ʾ���������
	unsigned int active_block_no_write;          //if a die has a active block, �����ʾ���������
	/*add  winks*/
	int64_t invaild_time;
	unsigned int gc_count;
	unsigned int blockIter;
};


struct blk_info{
	unsigned int erase_count;          //��Ĳ��������������¼��ram�У�����GC
	int free_page_num;        //��¼�ÿ��е�freeҳ������ͬ��
	int invalid_page_num;     //��¼�ÿ���ʧЧҳ�ĸ�����ͬ��
	int last_write_page;               //��¼���һ��д����ִ�е�ҳ��,-1��ʾ�ÿ�û��һҳ��д��
	struct page_info *page_head;       //��¼ÿһ��ҳ��״̬
	//=====================================================
	unsigned int free_lsb_num;
	unsigned int free_msb_num;
	unsigned int free_csb_num;
	unsigned int invalid_lsb_num;
	int last_write_lsb;
	int last_write_msb;
	int last_write_csb;
	int fast_erase;
	unsigned int fast_gc_count;
	unsigned int dr_state;
	unsigned int read_counts;
	unsigned int read_count;
	//=====================================================
	/*add by winks*/
	unsigned long changed_count;
	unsigned char write_frequently; // 0 is no, 1 is yes;

	int strip_page; 
};


struct page_info{                      //lpn��¼������ҳ�洢���߼�ҳ�������߼�ҳ��Чʱ��valid_state����0��free_state����0��
	int valid_state;                   //indicate the page is valid or invalid
	unsigned int free_state;                    //each bit indicates the subpage is free or occupted. 1 indicates that the bit is free and 0 indicates that the bit is used
	unsigned int lpn;                 
	unsigned int written_count;        //��¼��ҳ��д�Ĵ���
	unsigned int dadicate;             //0 least, 1 mid, 2 max
	unsigned int read_count;
	long long raidID;
};


struct dram_info{
	unsigned int dram_capacity;     
	int64_t current_time;

	struct dram_parameter *dram_paramters;      
	struct map_info *map;
	//struct buffer_info *buffer; 
	
	struct buffer_info_Hash *buffer; 
};


/*********************************************************************************************
*buffer�е���д�ص�ҳ�Ĺ�������:��buffer_info��ά��һ������:written����������ж��ף���β��
*ÿ��buffer management�У���������ʱ�����groupҪ�Ƶ�LRU�Ķ��ף�ͬʱ�����group���Ƿ�����
*д�ص�lsn���еĻ�����Ҫ�����groupͬʱ�ƶ���written���еĶ�β��������е������ͼ��ٵķ���
*����:����Ҫͨ��ɾ����д�ص�lsnΪ�µ�д�����ڳ��ռ�ʱ����written�������ҳ�����ɾ����lsn��
*����Ҫ�����µ�д��lsnʱ���ҵ�����д�ص�ҳ�������group�ӵ�ָ��written_insert��ָ����written
*�ڵ�ǰ��������Ҫ��ά��һ��ָ�룬��buffer��LRU������ָ�����ϵ�һ��д���˵�ҳ���´�Ҫ��д��ʱ��
*ֻ�������ָ����˵�ǰһ��groupд�ؼ��ɡ�
**********************************************************************************************/
typedef struct buffer_group{
	HASH_NODE node;                     //���ڵ�Ľṹһ��Ҫ�����û��Զ���ṹ����ǰ�棬ע��!
	struct buffer_group *LRU_link_next;	// next node in LRU list
	struct buffer_group *LRU_link_pre;	// previous node in LRU list

	unsigned int group;                 //the first data logic sector number of a group stored in buffer 
	unsigned int stored;                //indicate the sector is stored in buffer or not. 1 indicates the sector is stored and 0 indicate the sector isn't stored.EX.  00110011 indicates the first, second, fifth, sixth sector is stored in buffer.
	unsigned int dirty_clean;           //it is flag of the data has been modified, one bit indicates one subpage. EX. 0001 indicates the first subpage is dirty
	int flag;			                //indicates if this node is the last 20% of the LRU list	
	unsigned int read_count;
	unsigned int *accessCount;
	unsigned long long *accessTime;
	unsigned long long createTime;
}buf_node;


struct dram_parameter{
	float active_current;
	float sleep_current;
	float voltage;
	int clock_time;
};


struct map_info{
	struct entry *map_entry;            //������ӳ����ṹ��ָ��,each entry indicate a mapping information
	struct buffer_info *attach_info;	// info about attach map
};


struct controller_info{
	unsigned int frequency;             //��ʾ�ÿ������Ĺ���Ƶ��
	int64_t clock_time;                 //��ʾһ��ʱ�����ڵ�ʱ��
	float power;                        //��ʾ��������λʱ����ܺ�
};


struct request{
    /* Only real host writes own this record; job I/O borrows it. */
    struct experiment_request *experiment_stats;
	int64_t time;                      //���󵽴��ʱ�䣬��λΪus,�����ͨ����ϰ�߲�һ����ͨ������msΪ��λ��������Ҫ�и���λ�任����
	unsigned int lsn;                  //�������ʼ��ַ���߼���ַ
	unsigned int size;                 //����Ĵ�С���ȶ��ٸ�����
	unsigned int operation;            //��������࣬1Ϊ����0Ϊд

	/*************************/
	unsigned int priority;				//�����ж�SSD�Ƿ����
	/*************************/

	unsigned int* need_distr_flag;
	unsigned int complete_lsn_count;   //record the count of lsn served by buffer

	int distri_flag;		           // indicate whether this request has been distributed already

	int64_t begin_time;
	int64_t response_time;
	double energy_consumption;         //��¼��������������ģ���λΪuJ

	struct sub_request *subs;          //���ӵ����ڸ����������������
	struct request *next_node;         //ָ����һ������ṹ��
	unsigned int all;
	unsigned int now;
	unsigned char completeFlag;
	unsigned char MergeFlag;
};


struct sub_request{
	unsigned int lpn;                  //�����ʾ����������߼�ҳ��
	unsigned int ppn;                  //�����Ǹ�������ҳ�������������multi_chip_page_mapping�У�������ҳ����ʱ���ܾ�֪��psn��ֵ������ʱ��psn��ֵ��page_map_read,page_map_write��FTL��ײ㺯�������� 
	unsigned int operation;            //��ʾ������������ͣ����˶�1 д0�����в�����two plane�Ȳ��� 
	int size;

	unsigned int current_state;        //��ʾ��������������״̬�����궨��sub request
	int64_t current_time;
	unsigned int next_state;
	int64_t next_state_predict_time;
	 unsigned int state;              //ʹ��state�����λ��ʾ���������Ƿ���һ�Զ�ӳ���ϵ�е�һ�����ǵĻ�����Ҫ����buffer�С�1��ʾ��һ�Զ࣬0��ʾ����д��buffer
	                                  //��������Ҫ�����Ա��lsn��size�Ϳ��Էֱ����ҳ��״̬;����д������Ҫ�����Ա���󲿷�д������������bufferд�ز�����������������ҳ�������������������Ҫ����ά�ָó�Ա

	int64_t begin_time;               //������ʼʱ��
	int64_t complete_time;            //��¼��������Ĵ���ʱ��,������д����߶������ݵ�ʱ��

	struct local *location;           //�ھ�̬����ͻ�Ϸ��䷽ʽ�У���֪lpn��֪����lpn�÷��䵽�Ǹ�channel��chip��die��plane������ṹ�������������õ��ĵ�ַ
	struct sub_request *next_subs;    //ָ������ͬһ��request��������
	struct sub_request *next_node;    //ָ��ͬһ��channel����һ��������ṹ��
	struct sub_request *update;       //��Ϊ��д�����д��ڸ��²�������Ϊ�ڶ�̬���䷽ʽ���޷�ʹ��copyback��������Ҫ��ԭ����ҳ��������ܽ���д���������ԣ�������²����Ķ������������ָ����

	//*****************************************
	unsigned int target_page_type;
	unsigned int allocated_page_type;   //0 for lsb page, 1 for msb page;
	//*****************************************

	/*add for flag write freqently*/
	unsigned char write_freqently; // 0: no, 1:yes;
	unsigned char readXorFlag;
	unsigned int readRaidLpn;
	unsigned int readRaidLpnState;

	unsigned int raidNUM;
	unsigned char emergency;

	unsigned char degradeRead;
	unsigned char readoparity;
	struct request *req;
	struct sub_request* parent;

	unsigned long long blockCount;

	unsigned long long *completeCountR;
	unsigned long long *completeCountW;
	struct sub_request *preD;
	struct sub_request *NextD;
	int pChangeCount;
	unsigned char flagUser;
};


/***********************************************************************
*�¼��ڵ����ʱ���������ÿ��ʱ��������Ǹ���ʱ�������һ���¼���ȷ����
************************************************************************/
struct event_node{
	int type;                        //��¼���¼������ͣ�1��ʾ�������ͣ�2��ʾ���ݴ�������
	int64_t predict_time;            //��¼���ʱ�俪ʼ��Ԥ��ʱ�䣬��ֹ��ǰִ�����ʱ��
	struct event_node *next_node;
	struct event_node *pre_node;
};

struct parameter_value{
	unsigned int chip_num;          //��¼һ��SSD���ж��ٸ�����
	unsigned int dram_capacity;     //��¼SSD��DRAM capacity
	unsigned int cpu_sdram;         //��¼Ƭ���ж���

	unsigned int channel_number;    //��¼SSD���ж��ٸ�ͨ����ÿ��ͨ���ǵ�����bus
	unsigned int chip_channel[100]; //����SSD��channel����ÿchannel�Ͽ���������

	unsigned int die_chip;    
	unsigned int plane_die;
	unsigned int block_plane;
	unsigned int page_block;
	unsigned int subpage_page;

	unsigned int page_capacity;
	unsigned int subpage_capacity;

	/***********************������������**************************************/
	unsigned int turbo_mode;        //0 for off, 1 for always on, 2 for conditional on
	unsigned int turbo_mode_factor;
	unsigned int turbo_mode_factor_2;
	unsigned int lsb_first_allocation;
	unsigned int fast_gc;			//��¼�Ƿ�ʵʩ������������
	float fast_gc_thr_1;
	float fast_gc_filter_1;
	float fast_gc_thr_2;
	float fast_gc_filter_2;
	float fast_gc_filter_idle;
	float dr_filter;
	unsigned int dr_switch;
	unsigned int dr_cycle;
	/*************************************************************/

	unsigned int ers_limit;         //��¼ÿ����ɲ����Ĵ���
	int address_mapping;            //��¼ӳ������ͣ�1��page��2��block��3��fast
	int wear_leveling;              // WL�㷨
	int gc;                         //��¼gc����
	int clean_in_background;        //��������Ƿ���ǰ̨���
	int alloc_pool;                 //allocation pool ��С(plane��die��chip��channel),Ҳ����ӵ��active_block�ĵ�λ
	float overprovide;
	float gc_threshold;             //���ﵽ�����ֵʱ����ʼGC������������д�����У���ʼGC�����������ʱ�ж�GC�����������µ�����������ͨ�����У�GC�����ж�

	double operating_current;       //NAND FLASH�Ĺ���������λ��uA
	double supply_voltage;	
	double dram_active_current;     //cpu sdram work current   uA
	double dram_standby_current;    //cpu sdram work current   uA
	double dram_refresh_current;    //cpu sdram work current   uA
	double dram_voltage;            //cpu sdram work voltage  V

	int buffer_management;          //indicates that there are buffer management or not
	int scheduling_algorithm;       //��¼ʹ�����ֵ����㷨��1:FCFS
	float quick_radio;
	int related_mapping;

	unsigned int time_step;
	unsigned int small_large_write; //the threshould of large write, large write do not occupt buffer, which is written back to flash directly

	int striping;                   //��ʾ�Ƿ�ʹ����striping��ʽ��0��ʾû�У�1��ʾ��
	int interleaving;
	int pipelining;
	int threshold_fixed_adjust;
	int threshold_value;
	int active_write;               //��ʾ�Ƿ�ִ������д����1,yes;0,no
	float gc_hard_threshold;        //��ͨ�������ò����ò�����ֻ��������д�����У������������ֵʱ��GC���������ж�
	int allocation_scheme;          //��¼���䷽ʽ��ѡ��0��ʾ��̬���䣬1��ʾ��̬����
	int static_allocation;          //��¼�����־�̬���䷽ʽ����ICS09��ƪ�������������о�̬���䷽ʽ
	int dynamic_allocation;         //��¼��̬����ķ�ʽ
	int advanced_commands;  
	int ad_priority;                //record the priority between two plane operation and interleave operation
	int ad_priority2;               //record the priority of channel-level, 0 indicates that the priority order of channel-level is highest; 1 indicates the contrary
	int greed_CB_ad;                //0 don't use copyback advanced commands greedily; 1 use copyback advanced commands greedily
	int greed_MPW_ad;               //0 don't use multi-plane write advanced commands greedily; 1 use multi-plane write advanced commands greedily
	int aged;                       //1��ʾ��Ҫ�����SSD���aged��0��ʾ��Ҫ�����SSD����non-aged
	float aged_ratio; 
	int queue_length;               //������еĳ�������

	struct ac_time_characteristics time_characteristics;
};

/********************************************************
*mapping information,state�����λ��ʾ�Ƿ��и���ӳ���ϵ
*********************************************************/
struct entry{                       
	unsigned int pn;                //�����ţ��ȿ��Ա�ʾ����ҳ�ţ�Ҳ���Ա�ʾ������ҳ�ţ�Ҳ���Ա�ʾ�������
	unsigned int state;                      //ʮ�����Ʊ�ʾ�Ļ���0000-FFFF��ÿλ��ʾ��Ӧ����ҳ�Ƿ���Ч��ҳӳ�䣩�����������ҳ�У�0��1����ҳ��Ч��2��3��Ч�����Ӧ����0x0003.
	long long oldppa;
};


struct local{          
	unsigned int channel;
	unsigned int chip;
	unsigned int die;
	unsigned int plane;
	unsigned int block;
	unsigned int page;
	unsigned int sub_page;
};


struct gc_info{
	int64_t begin_time;            //��¼һ��planeʲôʱ��ʼgc������
	int copy_back_count;    
	int erase_count;
	int64_t process_time;          //��plane���˶���ʱ����gc������
	double energy_consumption;     //��plane���˶���������gc������
};


struct direct_erase{
	unsigned int block;
	struct direct_erase *next_node;
};


/**************************************************************************************
 *������һ��GC����ʱ��������ṹ������Ӧ��channel�ϣ��ȴ�channel����ʱ������GC��������
***************************************************************************************/
struct gc_operation{          
	unsigned int chip;
	unsigned int die;
	unsigned int plane;
	unsigned int block;           //�ò���ֻ�ڿ��жϵ�gc������ʹ�ã�gc_interrupt����������¼�ѽ��ҳ�����Ŀ����
	unsigned int page;            //�ò���ֻ�ڿ��жϵ�gc������ʹ�ã�gc_interrupt����������¼�Ѿ���ɵ�����Ǩ�Ƶ�ҳ��
	unsigned int state;           //��¼��ǰgc�����״̬
	unsigned int priority;        //��¼��gc���������ȼ���1��ʾ�����жϣ�0��ʾ���жϣ�����ֵ������gc����
	unsigned int wl_flag;
	struct gc_operation *next_node;
};

/*
*add by ninja
*used for map_pre function
*/
typedef struct Dram_write_map
{
	unsigned int state; 
}Dram_write_map;


struct ssd_info *initiation(struct ssd_info *);
struct parameter_value *load_parameters(char parameter_file[30]);
struct page_info * initialize_page(struct page_info * p_page);
struct blk_info * initialize_block(struct blk_info * p_block,struct parameter_value *parameter);
struct plane_info * initialize_plane(struct plane_info * p_plane,struct parameter_value *parameter );
struct die_info * initialize_die(struct die_info * p_die,struct parameter_value *parameter,long long current_time );
struct chip_info * initialize_chip(struct chip_info * p_chip,struct parameter_value *parameter,long long current_time );
struct ssd_info * initialize_channels(struct ssd_info * ssd );
struct dram_info * initialize_dram(struct ssd_info * ssd);

#endif

