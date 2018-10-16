#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <libusb-1.0/libusb.h>

#define VENDOR_ID	0x04d8
#define PRODUCT_ID	0xfd08

#define CONTROL_REQUEST_TYPE_IN (LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE)
#define CONTROL_REQUEST_TYPE_OUT (LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE)

#define EP_IN_ADDR	0x82
#define EP_OUT_ADDR	0x02

//	cdc-acm.h
#define ACM_CTRL_DTR   0x01
#define ACM_CTRL_RTS   0x02

//	cdc.h
#define USB_CDC_REQ_SET_LINE_CODING		0x20
#define USB_CDC_REQ_GET_LINE_CODING		0x21
#define USB_CDC_REQ_SET_CONTROL_LINE_STATE	0x22

libusb_context *ctx = NULL; 
libusb_device_handle *devh = NULL;

int trans_count = 0;
unsigned char trans_data[4092+1];	// 62x66

void dump(char cType,const char* data,int size);
void usage();

int toy_open()
{
	int ret;
	
	ret = libusb_init(&ctx);
	if(ret < 0)
	{
		fprintf(stderr, "ERROR:libusb initialize failed. %s\n",libusb_error_name(ret));
		return -1;
	}
	
	libusb_set_debug(NULL, 3);
	
	devh = libusb_open_device_with_vid_pid(NULL, VENDOR_ID, PRODUCT_ID);
	if(!devh)
	{
		fprintf(stderr, "ERROR:Can't Open irtoy. Device not found.n");
		return -1;
	}
	
	for (int if_num = 0; if_num < 2; if_num++) 
	{
		if (libusb_kernel_driver_active(devh, if_num)) 
		{
			libusb_detach_kernel_driver(devh, if_num);
		}
		ret = libusb_claim_interface(devh, if_num);
		if (ret < 0) 
		{
			fprintf(stderr, "ERROR:Can't claim interface. %s\n",libusb_error_name(ret));
			return -1;
		}
	}
	
	ret = libusb_control_transfer(devh, 
								  CONTROL_REQUEST_TYPE_OUT, USB_CDC_REQ_SET_CONTROL_LINE_STATE, 
								  ACM_CTRL_DTR | ACM_CTRL_RTS, 0, NULL, 0, 3000);
    if (ret < 0) 
	{
		fprintf(stderr, "ERROR:Can't set control line. %s\n",libusb_error_name(ret));
			return -1;
    }

    /* cdc.h - struct usb_cdc_line_coding
     * 115200 = 0x1C200 ~> 0x00, 0xC2, 0x01 in little endian
     */
    unsigned char encoding[] = { 0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x08 };
    ret = libusb_control_transfer(devh, 
								  CONTROL_REQUEST_TYPE_OUT, USB_CDC_REQ_SET_LINE_CODING, 
								  0, 0, encoding, sizeof(encoding), 3000);
    if (ret < 0) 
	{
		fprintf(stderr, "ERROR:Can't set baud rate. %s\n",libusb_error_name(ret));
			return -1;
    }
	
	return 0;
}

int toy_close()
{
	int ret = -1;
    if (devh)
	{
	    libusb_release_interface(devh, 0);
        libusb_close(devh);
		ret = 0;
	}
	if (ctx)
	{
    	libusb_exit(ctx);
	}
	
	return ret;
}

int toy_tciflush()
{
	unsigned char data[64];
    int length;
	int ret;
	int result = 0;
	
	while(1)
	{
    	ret = libusb_bulk_transfer(devh, EP_IN_ADDR, data, sizeof(data), &length,200);
		if (ret < 0 || length == 0) 
		{
			break;
		}
		result = 1;
	}
	return result;
}

int toy_puts(unsigned char * data, int size)
{
    int length;
	int ret;
	
	ret = libusb_bulk_transfer(devh, EP_OUT_ADDR, data, size, &length, 200);
    if (ret < 0) 
	{
		fprintf(stderr, "ERROR:while sending char. %s\n",libusb_error_name(ret));
		return -1;
    }
	return length;
}

int toy_gets(unsigned char * data, int size)
{
    int length,tmp;
	unsigned int timeout;
    int ret;
	
	length = 0;
	memset(data,0,size);
	while(1)
	{
		if(length == 0)
		{
			timeout = 200;
		}
		else
		{
			timeout = 1;
		}
		ret = libusb_bulk_transfer(devh, EP_IN_ADDR, data + length, size - length, &tmp, timeout);
		if (ret == LIBUSB_ERROR_TIMEOUT) 
		{
			if(length == 0)
			{
				fprintf(stderr, "ERROR:receive timeout. length=%d\n",length);
				return -1;
			}
			else
			{
				break;
			}
		} 
		else if (ret < 0) 
		{
			fprintf(stderr, "ERROR:while receiveing char. %s\n",libusb_error_name(ret));
			return -1;
		}
		length += tmp;
	}

    return length;
}

int toy_gets2(unsigned char * data, int size)
{
    int length,tmp;
	unsigned int timeout;
    int ret;
	
	length = 0;
	memset(data,0,size);
	while(1)
	{
		if(length == 0)
		{
			timeout = 200;
		}
		else
		{
			timeout = 1;
		}
		ret = libusb_bulk_transfer(devh, EP_IN_ADDR, data + length, size - length, &tmp, timeout);
		if (ret == LIBUSB_ERROR_TIMEOUT) 
		{
			if(length == 0)
			{
				//fprintf(stderr, "ERROR:receive timeout. length=%d\n",length);
				return -99;
			}
			else
			{
				break;
			}
		} 
		else if (ret < 0) 
		{
			fprintf(stderr, "ERROR:while receiveing char. %s\n",libusb_error_name(ret));
			return -1;
		}
		length += tmp;
	}

    return length;
}

int toy_cmd_res(const char* cmd1,char* data,int size,int  varbose)
{
	int result = -1;
	int ret;
	
	ret = toy_puts((char*)cmd1, strlen(cmd1));
	if(ret < 0)
	{
		goto EXIT_PATH;
	}
	if( varbose)
	{
		if(cmd1[strlen(cmd1)-1] != '\n')
		{
			fprintf(stderr,"S %s\n",cmd1);
		}
		else
		{
			fprintf(stderr,"S %s",cmd1);
		}
	}
	
	ret = toy_gets(data,size);
	if(ret < 0)
	{
		fprintf(stderr,"No response form IrToy.\n");
		goto EXIT_PATH;
	}
	if( varbose)
	{
		int len = strlen(data);
		if(data[len-1] != '\n')
		{
			fprintf(stderr,"R %s\n",data);
		}
		else
		{
			fprintf(stderr,"R %s",data);
		}
	}
	result = ret;
EXIT_PATH:
	return result;
}

int toy_reset(int varbose)
{
	int ret;
	char buf[32];

	for(int i = 0;i < 5;i++)
	{
		ret = toy_puts("\0",1);
		if(ret != 1)
		{
			fprintf(stderr,"ERROR:No Responce from IrToy.\n");
			return -1;
		}
		usleep(10 * 1000);		// 10mS Is this correct at this time?
	}	
	
	memset(buf,0,sizeof(buf));
	ret = toy_cmd_res("v",buf,sizeof(buf), varbose);
	if(ret < 4)
	{
		fprintf(stderr,"ERROR:Initialization failed IrToy.\n");
		return -1;
	}
	memset(buf,0,sizeof(buf));
	ret = toy_cmd_res("S",buf,sizeof(buf), varbose);
	if(ret != 3 && strcmp(buf,"S01"))
	{
		fprintf(stderr,"ERROR:Initialization failed IrToy.\n");
		return -1;
	}
	usleep(10 * 1000);		// 10mS Is this correct at this time?
	ret = toy_puts("\x24",1);
	if(ret != 1)
	{
		fprintf(stderr,"ERROR:No Responce from IrToy.\n");
		return -1;
	}
	usleep(10 * 1000);		// 10mS Is this correct at this time?
	ret = toy_puts("\x25",1);
	if(ret != 1)
	{
		fprintf(stderr,"ERROR:No Responce from IrToy.\n");
		return -1;
	}
	usleep(10 * 1000);		// 10mS Is this correct at this time?
	ret = toy_puts("\x26",1);
	if(ret != 1)
	{
		fprintf(stderr,"ERROR:No Responce from IrToy.\n");
		return -1;
	}
	usleep(10 * 1000);		// 10mS Is this correct at this time?
	return 0;
}

int toy_bcmd_res(unsigned char * cmd1, int sz,
				 unsigned char * res1, int* psz1,
				 int  varbose)
{
	int result = -1;
	int ret;
	
	ret = toy_puts((char*)cmd1, sz);
	if(ret < 0)
	{
		goto EXIT_PATH;
	}
	if( varbose)
	{
		dump('S', cmd1, sz);
	}
	
	ret = toy_gets(res1,*psz1);
	if(ret < 0)
	{
		*psz1 = 0;
		fprintf(stderr,"No response form IrToy.\n");
		goto EXIT_PATH;
	}
	*psz1 = ret;
	if( varbose)
	{
		dump('R', res1, ret);
	}
	result = 0;
EXIT_PATH:
	return result;
}

int toy_bcmd_res2(unsigned char * cmd1, int sz,
			 	  unsigned char * res1, int* psz1,
			 	  unsigned char * res2, int* psz2,
				 int  varbose)
{
	int result = -1;
	int ret;
	
	ret = toy_puts((char*)cmd1, sz);
	if(ret < 0)
	{
		goto EXIT_PATH;
	}
	if( varbose)
	{
		dump('S', cmd1, sz);
	}
	
	ret = toy_gets(res1,*psz1);
	if(ret < 0)
	{
		*psz1 = 0;
		fprintf(stderr,"No response form IrToy.\n");
		goto EXIT_PATH;
	}
	*psz1 = ret;
	if( varbose)
	{
		dump('R', res1, ret);
	}

	ret = toy_gets(res2,*psz2);
	if(ret < 0)
	{
		*psz2 = 0;
		fprintf(stderr,"No response form IrToy.\n");
		goto EXIT_PATH;
	}
	*psz2 = ret;
	if( varbose)
	{
		dump('R', res2, ret);
	}

	result = 0;
EXIT_PATH:
	return result;
}

int hex2ary(const char* hex)
{
	int ret,len;
	unsigned int x;
	char c;
	char tmp[4];
	
	trans_count = 0;
	memset(trans_data,0,sizeof(trans_data));
	
	memset(tmp,0,sizeof(tmp));
	len = strlen(hex);
	for(int i = 0;i < (len + 1);i++)
	{
		if(isspace(*hex) || 
		   i == len)
		{
			if(strlen(tmp))
			{
				ret = sscanf(tmp,"%x%c",&x,&c);
				if(ret == 1)
				{
					trans_data[trans_count] = (unsigned char)x;
					trans_count++;
					if(trans_count >= sizeof(trans_data))
					{
						fprintf(stderr,"ERROR:Hex string is too long.\n");
						return -1;
					}
					memset(tmp,0,sizeof(tmp));
				}
				else
				{
					fprintf(stderr,"ERROR:Incorrect hex number. %s\n",tmp);
					return -1;
				}
			}
		}
		else
		{
			int len = strlen(tmp);
			tmp[len] = *hex;
			if(len >= 3)
			{
				fprintf(stderr,"ERROR:Hex digit is too long.\n");
				return -1;
			}
		}
		hex++;
	}
	if(trans_count >= 2)
	{
		if(	trans_data[trans_count - 1] != 0xff ||  
			trans_data[trans_count - 2] != 0xff)
		{
			if(trans_count <= (sizeof(trans_data) - 2))
			{
				trans_data[trans_count] = 0xff;
				trans_count++;
				trans_data[trans_count] = 0xff;
				trans_count++;
				fprintf(stderr,"waring:Adjust terminate 0xff,0xff.\n");
			}
		}
	}
	fprintf(stderr,"%d bytes\n",trans_count);
	return 0;
}

// int trans_count = 0;
// unsigned char trans_data[1024];
int toy_transfer(const char* hex, int varbose)
{
	int res = -1;
	int ret;
	int sz1,sz2;
	char res1[32];
	char res2[32];
	
	ret = hex2ary(hex);
	if(ret)
	{
		goto EXIT_PATH;
	}
	ret = toy_open();
	if(ret)
	{
		goto EXIT_PATH;
	}
	ret = toy_tciflush();
	if(ret)
	{
		goto EXIT_PATH;
	}
	ret = toy_reset(varbose);
	if(ret)
	{
		goto EXIT_PATH;
	}
	
	fprintf(stderr, "Initialize complate.\n");

	sz1 = sizeof(res1);
	memset(res1,0,sizeof(res1));
	ret = toy_bcmd_res("\x3", 1,
		 res1,&sz1,
		 varbose);
	if(ret || sz1 != 1 || res1[0] != 0x3E)
	{
		goto EXIT_PATH;
	}

	for(int i = 0;i < trans_count;i+=62)
	{
		if(trans_count <= (i + 62))
		{
			sz1 = sizeof(res1);
			memset(res1,0,sizeof(res1));
			sz2 = sizeof(res2);
			memset(res2,0,sizeof(res2));
			ret = toy_bcmd_res2(&trans_data[i], 
				(trans_count % 62) ? (trans_count % 62) : 62,
				res1,&sz1,
				res2,&sz2,
				varbose);
			if(ret || 
			   (sz1 != 1 || res1[0] != 0x3E) ||
			   (sz2 <  4 || res2[0] != 't') ||
			               (res2[3] != 'C') )
			{
				goto EXIT_PATH;
			}
		}
		else
		{		
			sz1 = sizeof(res1);
			memset(res1,0,sizeof(res1));
			ret = toy_bcmd_res(&trans_data[i], 62,
				 res1,&sz1,
				 varbose);
			if(ret || sz1 != 1 || res1[0] != 0x3E)
			{
				goto EXIT_PATH;
			}
		}
	}
	fprintf(stderr,"SUCCESS:Tansfer %d bytes\n",trans_count);
	res = 0;
EXIT_PATH:
	toy_close();
	return res;
}

int toy_receive(int varbose)
{
	int res = -1;
	int ret;
	unsigned char rbuf[4092];
	int off,lastoff,rsz,tmout;
	
	ret = toy_open();
	if(ret)
	{
		return -1;
	}
	ret = toy_tciflush();
	if(ret)
	{
		return -1;
	}
	ret = toy_reset(varbose);
	if(ret)
	{
		return -1;
	}
	
	fprintf(stderr, "Initialize complate.\n");

	memset(rbuf,0,sizeof(rbuf));
	off = 0;
	lastoff = 0;
	rsz = sizeof(rbuf);
	tmout = 0;
	while(1)
	{
		ret = toy_gets2(&rbuf[off],rsz);
		if(ret >= 0)
		{
			lastoff = off;
			off += ret;
			rsz -= ret;
			
			if(varbose)
			{
				dump('R',&rbuf[lastoff],ret);
			}

			if(	rbuf[off - 1] == 0xff &&  
				rbuf[off - 2] == 0xff)
			{
				fprintf(stderr, "SUCCESS:Receive complate. length=%d\n",off);
				res = 0;
				break;
			}
			if(rsz == 0)
			{
				fprintf(stderr, "ERROR:receive buffer overrun. length=%d\n",off);
				break;
			}
		}
		else if(ret == -99)
		{
			tmout ++;
			if(tmout >= (3000 / 200))
			{
				fprintf(stderr, "ERROR:receive timeout. length=%d\n",off);
				break;
			}
		}
		else if(ret < 0)
		{
			break;
		}
	}
EXIT_PATH:
	if((res == 0) && (off != 0))
	{
		for(int i = 0;i < off;i++)
		{
			printf("%02X ",((unsigned int)rbuf[i]) & 0xff);
		}
		printf("\n");
	}
	toy_close();
	return 0;
}

int main(int argc,char* argv[])
{
	int ret = -1;
	int varbose = 0;
	char cmd = '\0';
	char* hex = NULL;
	
	int opt;
	while ((opt = getopt(argc, argv, "vrt:")) != -1) 
	{
        switch (opt) 
		{
            case 'r':
				cmd = opt;
                break;
            case 'v':
				varbose = 1;
                break;
            case 't':
				cmd = opt;
				hex = optarg;
                break;
        }
    }

	if(cmd == '\0')
	{
		usage();
		return 0;
	}
	
	switch(cmd)
	{
		case 'r': 
			ret = toy_receive(varbose);
			break;
		case 't': 
			ret = toy_transfer(hex,varbose);
			break;
	}
	
	return 0;
}

void usage() 
{
  fprintf(stderr, "usage: toy_ir_cmd <option>\n");
  fprintf(stderr, "  -r       \tReceive Infrared code.\n");
  fprintf(stderr, "  -t 'hex'\tTransfer Infrared code.\n");
  fprintf(stderr, "  -t \"$(cat XXX.txt)\"\n");
  fprintf(stderr, "  -t \"`cat XXX.txt`\"\n");
  fprintf(stderr, "  -v       \t varbose mode.\n");
}

void dump(char cType, const char* data,int size)
{
	char * poi;
	char c;
	int i,j;
	for(i = 0;i < size;i+=16)
	{
		fprintf(stderr,"%c %04X ",cType,i);
		for(j = 0;j < 16;j++)
		{
			if((i + j) >= size)
			{
				break;
			}
			c = data[i + j];
			fprintf(stderr,"%02X ",(int)c & 0xFF);
			if(j == 7)
			{
			 fprintf(stderr,"- ");
			}
		}
		for(/*j = 0*/;j < 16;j++)
		{
			fprintf(stderr,"   ");
			if(j == 7)
			{
				fprintf(stderr,"- ");
			}
		}
		for(j = 0;j < 16;j++)
		{
			if((i + j) >= size)
			{
				break;
			}
			c = data[i + j];
			fprintf(stderr,"%c",isprint(c) ? c : '?');
			if(j == 7)
			{
				fprintf(stderr," ");
			}
		}
		for(/*j = 0*/;j < 16;j++)
		{
			fprintf(stderr," ");
			if(j == 7)
			{
				fprintf(stderr," ");
			}
		}
		fprintf(stderr,"\n");
	}
}

