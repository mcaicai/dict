#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <sqlite3.h>
#include <signal.h>
#include <time.h>

#define DATABASE "my.db"
#define N 32
#define R 1 //user - register
#define L 2 //user - login
#define Q 3 //user - query
#define H 4 //user - history


typedef struct {
	int type;
	char name[N];
	char data[256];
}MSG;


int do_client(int acceptfd, sqlite3 *db);
int do_login(int acceptfd,MSG *msg, sqlite3 *db);
void do_register(int acceptfd,MSG *msg, sqlite3 *db);
int do_query(int acceptfd,MSG *msg, sqlite3 *db);
int do_history(int acceptfd,MSG *msg,sqlite3 *db);
int history_callback(void *arg,int f_num,char** f_value,char **f_name);
int do_searchword(int acceptfd,MSG *msg,char word[]);
int get_date(char *date);



// 定义通信双方的信息结构体


//./server 192.168.1.117 10000
int main(int argc,char *argv[])
{
	int sockfd;
	struct sockaddr_in serveraddr;
	MSG msg;
	int n;
	sqlite3 *db;
	int acceptfd;
	pid_t pid;

	if(argc!=3)
	{
		printf("Usage : %s server ip port .\n",argv[0]);
		return -1;
	}
	else
	{
		printf("success open.\n");
	}

	if(sqlite3_open(DATABASE,&db)!=SQLITE_OK)
	{
		printf("%s\n",sqlite3_errmsg(db));
		return -1;
	}
	else
	{
		printf("open DATABASE success\n");
	}
	if((sockfd = socket(AF_INET,SOCK_STREAM,0)) < 0)
	{
		perror("fail to socket\n");
		return -1;
	}
	bzero(&serveraddr , sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(argv[1]);
	serveraddr.sin_port = htons(atoi(argv[2]));
	if(bind(sockfd,(struct sockaddr *)&serveraddr,sizeof(serveraddr))<0)
	{
		perror("fail to bind.\n");
		return -1;
	}

	//将套接字设为监听模式
	if(listen(sockfd,5)<0)
	{	
		printf("fail to listen.\n");
		return -1;
	}

	//处理僵尸进程
	signal(SIGCHLD,SIG_IGN);

	while(1)
	{
		if((acceptfd = accept(sockfd,NULL,NULL))<0)
		{
			perror("fail to accept");
			return -1;
		}
		if((pid = fork())<0)
		{
			perror("fail to fork");
			return -1;
		}
		else if(pid == 0)	//儿子进程,处理客户端的具体信息
		{
			close(sockfd);
			do_client(acceptfd,db);
		}
		else			//父亲进程,用来接收客户端的请求的
		{
			close(acceptfd);
		}	
	}

	return 0;
}

int do_client(int acceptfd, sqlite3 *db)
{
	MSG msg;
	while(recv(acceptfd,&msg,sizeof(msg),0)>0)
	{	
		switch(msg.type)
		{
			printf("type = %d\n",msg.type);
			case R:
				do_register(acceptfd,&msg,db);
				break;
			case L:
				do_login(acceptfd,&msg,db);
				break;
			case Q:
				do_query(acceptfd,&msg,db);
				break;
			case H:
				do_history(acceptfd,&msg,db);
				break;
			default:
				printf("Invaild datamsg.\n");
		}
	}
	printf("client exit!\n");
	close(acceptfd);
	exit(0);
	return 0;

}

void do_register(int acceptfd,MSG *msg,sqlite3 *db)
{
	char * errmsg;
	char sql[128];
	sprintf(sql,"insert into usr values('%s',%s);",msg->name,msg->data);
	printf("%s\n",sql);
	if(sqlite3_exec(db,sql,NULL,NULL,&errmsg)!= SQLITE_OK)
	{
		printf("%s\n",errmsg);
		strcpy(msg->data,"usr name already exist.");
	}
	else
	{
		printf("client register ok!\n");
		strcpy(msg->data,"OK");
	}
	if(send(acceptfd,msg,sizeof(MSG),0)<0)
	{
		perror("fail to send");
		exit(1); 
	}
	exit(1);
}

int do_login(int acceptfd,MSG *msg, sqlite3 *db)
{
	char sql[128] = {};
	char *errmsg;
	int nrow;
	int ncloumn;
	char **resultp;
	sprintf(sql,"select * from usr where name = '%s' and pass = '%s';",msg->name,msg->data);
	printf("%s\n",sql);
	
	if(sqlite3_get_table(db,sql,&resultp,&nrow,&ncloumn,&errmsg)!=SQLITE_OK)
	{
		printf("%s\n",errmsg);
		return -1;
	}	
	else
	{
		printf("get_table ok!");
	}
	//查询成功，数据库中拥有此用户
	if(nrow == 1)
	{
		strcpy(msg->data,"OK");
		send(acceptfd,msg,sizeof(MSG),0);
		return 1;
	}
	//密码或者用户名错误	
	else 
	{	
		strcpy(msg->data,"usr / passwd wrong.");
		send(acceptfd,msg,sizeof(MSG),0);
	}	
	return 0;
} 

int do_searchword(int acceptfd,MSG *msg,char word[])
{
	FILE *fp;
	int len =0;
	char temp[512]={};
	int result = 0;
	char *p;
	//打开文件，读取文件，进行比对
	if((fp = fopen("dict.txt","r"))==NULL)
	{
		perror("fail to fopen\n");
		strcpy(msg->data,"fail to open dict.txt");
		send(acceptfd,msg,sizeof(MSG),0);
		return -1;
	}
	//打印出客户端要查询的单词
	len = strlen(word);
	printf("%s, len = %d\n",word,len);
	//读文件，来查询单词
	while(fgets(temp,512,fp)!=NULL)
	{
		result = strncmp(temp,word,len);
	
		if(result<0)
		{
			continue;
		}
		if(result>0 ||( (result == 0)&&(temp[len]!=' ')))
		{
			break;
		}
		//表示找到了，查询的单词
		p = temp + len;
		while(*p==' ')
		{
			p++;
		}
		//找到了注释,跳跃过了所有的空格
		strcpy(msg->data,p);
		fclose(fp);
		return 1;
	
	}
	fclose(fp);
	return 0;
}

int get_date(char *date)
{
	time_t t;
	struct tm *tp;
	time(&t);
	//进行时间的格式转换
	tp = localtime(&t);
	sprintf(date,"%d-%d-%d %d:%d:%d",tp->tm_year + 1900,tp->tm_mon + 1,tp->tm_mday,
					tp->tm_hour,tp->tm_min ,tp->tm_sec);
	return 0;
	
}

int do_query(int acceptfd,MSG *msg,sqlite3 *db)
{
	char word[64];
	int found = 0;
	char date[128];
	char sql[128];
	char *errmsg;
	//拿出msg结构体中，要查询的单词
	strcpy(word,msg->data);

	found = do_searchword(acceptfd,msg,word);
	//表示找到了单词，那么此时应该将用户名，时间，单词，插入到历史记录表中去

	if(found ==1)
	{
		//需要获取系统时间
		get_date(date);
		sprintf(sql,"insert into record values('%s', '%s','%s')",msg->name,date,word);
		printf("%s\n",sql);
		if(sqlite3_exec(db,sql,NULL,NULL,&errmsg)!=SQLITE_OK)
		{
			printf("%s\n",errmsg);
			return -1;
		}
	}
	else   		//表示没有找到
	{
		strcpy(msg->data,"Not found!");
	}
	//将查询结果，发送给客户端
	send(acceptfd,msg,sizeof(MSG),0);
	return 0;
}


//得到查询结果,并且需要将历史记录发送给客户端
int history_callback(void *arg,int f_num,char** f_value,char **f_name)
{
	int acceptfd;
	MSG msg;

	acceptfd = *((int *)arg);
	sprintf(msg.data,"%s ,%s",f_value[1],f_value[2]);
	send(acceptfd,&msg,sizeof(MSG),0);
	return 0;
}

int do_history(int acceptfd,MSG *msg,sqlite3 *db)
{
	char sql[128] = {};
	char *errmsg;
	sprintf(sql,"select * from record where name = '%s'",msg->name);

	//查询数据库
	if(sqlite3_exec(db,sql,history_callback,(void *)&acceptfd,&errmsg)!=SQLITE_OK)
	{
		printf("%s\n",errmsg);
	}
	else
	{
		printf("Query record done.\n");
	}
	//所有的记录查询发送完毕后，给客户端发送一个结束信息
	msg->data[0]='\0';
	send(acceptfd,msg,sizeof(MSG),0); 
	
	return 0;
} 



