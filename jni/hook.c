/*
 * ���ļ���������Ҫ����
 * 1����ȡold_open��old_close�ĺ�����ַ�Լ��Լ������new_open��new_close�ĺ�����ַ������
 * 2���µ�open��close��������Ҫ���̣���open��ʱ�򣬲���·��������ƥ�䣬����Ҫ���ܣ�����н��ܵ���ʱ�ļ���Ȼ����ʱ�ļ������������أ�
 * 	 ��close��ʱ�򣬽����ж��Ƿ�Ϊ�����ļ������ǣ�����м��ܲ�������ɾ����ʱ�ļ���
 * 3���ӽ���ģ��ĳ���
 */
#include <unistd.h>
#include <sys/types.h>
#include <android/log.h>
#include <linux/binder.h>
#include <stdio.h>
#include <stdlib.h>
#include <asm/ptrace.h>
#include <asm/user.h>
#include <asm/ptrace.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <jni.h>
#include <android/log.h>
#include <sys/system_properties.h>
#include <openssl/evp.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/stat.h>
#define PROPERTY_VALUE_MAX 256
/*
 * JNI����־���
 */
#define LOG_TAG "inject"
#define LOGD(fmt, args...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, fmt, ##args)

/*	��������С*/
#define BUFFER_SIZE 1024

/*	ö�ٿ�ѡ�ļ����㷨	*/
enum crypt {
	Aes_256_ofb, //256λAES�㷨�������㷨��0.9.7�汾,64λ���������OutputFeedback�����ܷ�ʽ
	Aes_192_ofb, //OFB��ʽ��192λAES�㷨
	Aes_128_ofb, //OFB��ʽ��128λAES�㷨
	Des_ede3_ofb, //OFB��ʽ��3DES�㷨���㷨��������Կ������ͬ
	Des_ede_ofb, //OFB��ʽ��3DES�㷨���㷨�ĵ�һ����Կ�����һ����Կ��ͬ����ʵ�Ͼ�ֻ��Ҫ������Կ
	Rc2_ofb, //OFB��ʽ��RC2�㷨�����㷨����Կ�����ǿɱ�ģ�����ͨ��������Ч��Կ���Ȼ���Ч��Կλ�����ò������ı䡣ȱʡ����128λ��
	Bf_ofb, //OFB��ʽ��Blowfish�㷨�����㷨����Կ�����ǿɱ��
	Enc_null //���㷨�����κ����飬Ҳ����û�н��м��ܴ���
};

/*
 * ��������
 */
int encrypt_init(EVP_CIPHER_CTX *ctx);
int encrypt_abstract(const char *plaintext_path, EVP_CIPHER_CTX *ctx,
		const char *ciphertext_path);
int decrypt_init(EVP_CIPHER_CTX *ctx);
int decrypt_abstract(const char *plaintext_path, EVP_CIPHER_CTX *ctx,
		const char *ciphertext_path);
char *get_key(int key_length);
enum crypt get_crypt_config(char *config_name);
int check_path(const char* path);
char* create_tmpfile(const char* path);
char* recover_tmpfile(const char* path);

/*
 * ���hook֮ǰ��open �� close ������ַ���ڼӽ��ܲ�������������������ص������ĵ����С�
 */
extern int __open(const char*, int, int);
int (*old_open)(const char* path, int mode, ...) = open;
int (*old_close)(int fd)=close;

int call_count = 0;
// ������open���º�����ַ�������ڲ��������ϵ�open
int new_open(const char* path, int mode, ...) {

	//for test
	LOGD("[+]-----------new open test txt file-----------");
	LOGD("[+] The New open path %s", path);
	call_count++;
	LOGD("[+] The New open count %d", call_count);
	LOGD("[+] The OLD open real path %x", old_open);
	LOGD("[+] The NEW open real path %x", new_open);

	// ���path�Ƿ�Ϊ����б��еĵ�ַ
	//const char* to_path = "/mnt/sdcard/owncloud/admin@192.168.111.11/test.txt";
	int check = check_path(path);
	LOGD("[+] check %d", check);
	//temp�ļ�������
	//	const char* de_path = "/mnt/sdcard/owncloud/admin@192.168.111.11/testcopy1.txt";

	if (check > 0) {
		LOGD("[+] open the file");
		//temp�ļ�������

		const char* de_path = create_tmpfile(path);
		LOGD("[+] Create tmp open file fd %s", de_path);
		EVP_CIPHER_CTX ctx;

		/* ���� */
		decrypt_init(&ctx);
		decrypt_abstract(de_path, &ctx, path);

		//�����ܵ��ļ�����
		int res = (*old_open)(de_path, mode); //
		LOGD("[+] The New open file fd %d", res);
		//��ӡ�Ľ����from_fd ����to_fd????

		/* �жϸ�fd�������ļ���ȷ�����µĽ����ļ� */
		char s[256], name[256];
		snprintf(s, 255, "/proc/%d/fd/%d", getpid(), res);
		memset(name, 0, sizeof(name)); // readlink��name���治���'\0'��������buf
		readlink(s, name, 255);
		LOGD("[+] The Name of fd %s", name);
		LOGD("[+] The S of fd %s", s);

		return res;
	}

	//������Ҫ���ܵ��ļ���ֱ��ͨ��old_open����
	int res = (*old_open)(path, mode); //?????
	LOGD("[+] The old openfile fd %d", res);
	return res;

}

int new_close(int fd) {
	LOGD("[+]-----------new close test txt file-----------");
	LOGD("[+] The OLD close real path %x", old_close);
	LOGD("[+] The NEW close real path %x", new_close);
	//ͨ��fd�������ļ����ж��Ƿ��ǽ��ܺ���ļ���
	char s[256], name[256];
	snprintf(s, 255, "/proc/%d/fd/%d", getpid(), fd);
	memset(name, 0, sizeof(name)); // readlink��name���治���'\0'��������buf
	readlink(s, name, 255);
	LOGD("[+] The Name of fd %s", name);
	LOGD("[+] The S of fd %s", s);

	//2.
	const char* from_path = name;
	const char* to_path = recover_tmpfile(name);
	LOGD("[+] The path of rec %s\n", to_path);
	int check = 0;
	LOGD("[+] The check1 %d\n", check);
	if (to_path == NULL) {
		check = 0;
		LOGD("[+] The check2 %d\n", check);
	} else {
		check = check_path(to_path);
		LOGD("[+] The check3 %d\n", check);
	}
	if (check) {
		LOGD("[+] open pre test copy file");

		/* ����*/
		EVP_CIPHER_CTX ctx;
		encrypt_init(&ctx);
		encrypt_abstract(from_path, &ctx, to_path);

		/* �رռ����ļ� */
		int res = (*old_close)(fd);
		LOGD("[+] The new close file return %d.", res);
		return res;
	}
	//4.invoke old function
	int res = (*old_close)(fd);
	LOGD("[+] The old close file return %d.", res);

	/*	ɾ����ʱ�ļ� */
//	int removefd=remove(new_path);
//	LOGD("[+] The temp file has been removed %d.",removefd);
	return res;
}

int do_hook(unsigned long * old_open_addr, unsigned long * new_open_addr,
		unsigned long * old_close_addr, unsigned long * new_close_addr) {

	LOGD("[+] do_hook function is invoked ");
	old_open = open;
	old_close = close;
	LOGD("[+] open addr: %p. New addr %p\n", open, new_open);

	//get open function address
	char value[PROPERTY_VALUE_MAX] = { '\0' };
	snprintf(value, PROPERTY_VALUE_MAX, "%u", old_open);
	*old_open_addr = old_open;
	LOGD("[+] just for test print old_open address %p\n", *old_open_addr);
//	snprintf(value, PROPERTY_VALUE_MAX, "%u", new_open);
	*new_open_addr = new_open;
	LOGD("[+] just for test print new_open address %p\n", *new_open_addr);

	//get close function address
	*old_close_addr = old_close;
	LOGD("[+] just for test print old_close address %p\n", *old_close_addr);
	*new_close_addr = new_close;
	LOGD("[+] just for test print new_close address %p\n", *new_close_addr);

	return 0;
}

/*
 * ��Կ��ȡ�ӿ�
 */
char *get_key(int key_length) {
	char *key = "1234567890A";
	return key;
}

/*
 * ��ȡ���ã����ѡȡ�ļӽ���ģʽ
 */
enum crypt get_crypt_config(char *config_name) {
	return Aes_192_ofb;
}

/*
 * ��ʼ������������
 * ��ʼ���ɹ�����	1����ʼ��ʧ�ܷ���	-1
 */
int encrypt_init(EVP_CIPHER_CTX *ctx) {
	int ret, key_len, i;
	const EVP_CIPHER *cipher;
	unsigned char iv[8];

	/*	������ʼ�����˲���Ϊ�������	*/
	for (i = 0; i < 8; i++) {
		memset(&iv[i], i, 1);
	}
	const char *key = get_key(key_len);

	/*
	 *���ܳ�ʼ�����������������þ����㷨�� init �ص���������������Կ key ת��Ϊ�ڲ���Կ��ʽ��
	 *���ܳ�ʼ�����������������þ����㷨�� ����ʼ������iv ������ctx �ṹ�С�
	 */
	EVP_CIPHER_CTX_init(ctx);

	/*	��ȡ�ӽ�������,����һ��EVP_CIPHER 	*/
//	cipher = EVP_enc_null();
	enum crypt ciphername;
	ciphername = get_crypt_config("xml");
	switch (ciphername) {
	case Aes_256_ofb:
		cipher = EVP_aes_256_ofb();
		break;
	case Aes_192_ofb:
		cipher = EVP_aes_192_ofb();
		break;
	case Aes_128_ofb:
		cipher = EVP_aes_128_ofb();
		break;
	case Des_ede_ofb:
		cipher = EVP_des_ede_ofb();
		break;
	case Des_ede3_ofb:
		cipher = EVP_des_ede3_ofb();
		break;
	case Rc2_ofb:
		cipher = EVP_rc2_ofb();
		break;
	case Bf_ofb:
		cipher = EVP_bf_ofb();
		break;
	case Enc_null:
	default:
		cipher = EVP_enc_null();
		break;

	}
	ret = EVP_EncryptInit_ex(ctx, cipher, NULL, key, NULL);
	if (ret != 1) {
		LOGD("EncryptInit err!\n");
		return -1;
	}
	return 1;
}
/*
 * ������ܺ���
 * ���ܳɹ��򷵻����ĳ���
 * ����ʧ���򷵻�-1
 */
int encrypt_abstract(const char *plaintext_path, EVP_CIPHER_CTX *ctx,
		const char *ciphertext_path) {
	int bytes_read, bytes_write, i;
	int from_fd, to_fd;
	unsigned char in[BUFFER_SIZE], out[BUFFER_SIZE];
	for (i = 0; i < BUFFER_SIZE; i++) {
		in[i] = '\0';
	}
	int inl = BUFFER_SIZE;
	int outl = 0;
	int len = 0;

	/*	������·���ļ��ʹ���Ҫ��ŵ�����·�����ļ�	*/
	from_fd = (*old_open)(plaintext_path, O_RDONLY, 0);
	to_fd = (*old_open)(ciphertext_path, O_WRONLY | O_CREAT | O_TRUNC);
	if (from_fd < 0) {
		LOGD("Unable to open file %s", plaintext_path);
		return -1;
	} else if (to_fd < 0) {
		LOGD("Unable to open file %s", ciphertext_path);
		return -1;
	}

	/*
	 * ����openssl��EVPģʽ���мӽ���
	 * EVP_EncryptUpdate ���ܺ��������ڶ�μ��㣬�������˾����㷨��do_cipher�ص�������
	 * EVP_EncryptFinal ��ȡ���ܽ�������������漰��䣬�������˾����㷨��do_cipher�ص�������
	 */
	int final = 0;
	while (bytes_read = read(from_fd, in, BUFFER_SIZE)) {
//		LOGD("bytes_read:%d\n", bytes_read);
		if ((bytes_read == -1) && (errno != EINTR)) {
			printf("Error\n");
			break;
		} else if (bytes_read == BUFFER_SIZE) {
//			LOGD("read bytes :\n%s\n", in);
			EVP_EncryptUpdate(ctx, out, &outl, in, inl);
			len += outl;
		} else if ((bytes_read < BUFFER_SIZE) && len == 0) {
//			LOGD("short read:\n%s\n", in);
			EVP_EncryptUpdate(ctx, out, &outl, in, inl);
			len += outl;
			EVP_EncryptFinal_ex(ctx, out + len, &outl);
			len += outl;
		} else if ((bytes_read == 1) && len != 0) {
			final = 1;
//			LOGD("256 read:\n%s\n", in);
			EVP_EncryptFinal_ex(ctx, out, &outl);
			len += outl;
		} else {
//			LOGD("final read:\n%s\n", in);
			EVP_EncryptUpdate(ctx, out, &outl, in, inl);
			len += outl;
			EVP_EncryptFinal_ex(ctx, out, &outl);
			len += outl;
		}
		if (final != 1)
			bytes_write = write(to_fd, out, BUFFER_SIZE);

		/*��ֹ������δ���㣬�Զ�ȡ�ĸ���*/
		memset(out, '\0', sizeof(char) * BUFFER_SIZE);
		memset(in, '\0', sizeof(char) * BUFFER_SIZE);
	}

	(*old_close)(from_fd);
	(*old_close)(to_fd);

	/*
	 * ����Գ��㷨���������ݣ��������û��ṩ�����ٺ�����������е��ڲ���Կ�Լ��������ݡ�
	 */
	EVP_CIPHER_CTX_cleanup(ctx);
	LOGD("���ܽ�����ȣ�%d\n", len);
	return len;
}

/*
 * ��ʼ������������
 */
int decrypt_init(EVP_CIPHER_CTX *ctx) {
	int ret, key_len, i;
	const EVP_CIPHER *cipher;
	unsigned char iv[8];
	for (i = 0; i < 8; i++) {
		memset(&iv[i], i, 1);
	}
	const char *key = get_key(key_len);
	EVP_CIPHER_CTX_init(ctx);
	enum crypt ciphername;
	ciphername = get_crypt_config("xml");
	switch (ciphername) {
	case Aes_256_ofb:
		cipher = EVP_aes_256_ofb();
		break;
	case Aes_192_ofb:
		cipher = EVP_aes_192_ofb();
		break;
	case Aes_128_ofb:
		cipher = EVP_aes_128_ofb();
		break;
	case Des_ede_ofb:
		cipher = EVP_des_ede_ofb();
		break;
	case Des_ede3_ofb:
		cipher = EVP_des_ede3_ofb();
		break;
	case Rc2_ofb:
		cipher = EVP_rc2_ofb();
		break;
	case Bf_ofb:
		cipher = EVP_bf_ofb();
		break;
	case Enc_null:
	default:
		cipher = EVP_enc_null();
		break;

	}
	ret = EVP_EncryptInit_ex(ctx, cipher, NULL, key, NULL);
	if (ret != 1) {
		LOGD("EVP_DecryptInit_ex err1!\n");
		return -1;
	}
}

/*
 * ������ܺ���
 * ���ܳɹ��򷵻����ĳ���
 * ����ʧ���򷵻�-1
 */
int decrypt_abstract(const char *plaintext_path, EVP_CIPHER_CTX *ctx,
		const char *ciphertext_path) {
	int bytes_read, bytes_write, i;
	int to_fd, de_fd;
	unsigned char in[BUFFER_SIZE], de[BUFFER_SIZE];
	for (i = 0; i < BUFFER_SIZE; i++) {
		in[i] = '\0';
	}
	int inl = BUFFER_SIZE;
	int outl = 0;
	int len = 0;

	/*	������·���ļ��ʹ���Ҫ��ŵ�����·�����ļ�	*/
	to_fd = (*old_open)(ciphertext_path, O_RDONLY, 0);
	de_fd = (*old_open)(plaintext_path, O_WRONLY | O_CREAT | O_TRUNC);
	if (de_fd < 0) {
		LOGD("Unable to open file %s", plaintext_path);
		return -1;
	} else if (to_fd < 0) {
		LOGD("Unable to open file %s", ciphertext_path);
		return -1;
	}
	memset(in, 0, sizeof(char) * BUFFER_SIZE);
	while (bytes_read = read(to_fd, in, BUFFER_SIZE)) {
//		LOGD("bytes_read:%d\n", bytes_read);
		if ((bytes_read == -1) && (errno != EINTR)) {
//			LOGD("Error\n");
			break;
		} else if (bytes_read == BUFFER_SIZE) {
//			LOGD("read bytes : %s\n", in);
			EVP_DecryptUpdate(ctx, de, &outl, in, inl);
			len += outl;
		} else if ((bytes_read < BUFFER_SIZE) && len == 0) {
//			LOGD("short read:\n%s\n", in);
			EVP_DecryptUpdate(ctx, de, &outl, in, inl);
			len += outl;
			EVP_DecryptFinal_ex(ctx, de + len, &outl);
			len += outl;
		} else {
			EVP_DecryptFinal_ex(ctx, de, &outl);
			len += outl;
		}
//		LOGD("the decrypt:\n%s\n", de);
		bytes_write = write(de_fd, de, BUFFER_SIZE);
		memset(de, '\0', sizeof(char) * BUFFER_SIZE);
		memset(in, '\0', sizeof(char) * BUFFER_SIZE);
	}

	(*old_close)(de_fd);
	(*old_close)(to_fd);
	LOGD("���ܽ�����ȣ�%d\n", len);
	EVP_CIPHER_CTX_cleanup(ctx);

	return len;
}

/*
 * �������ļ��в����Ƿ��·��Ϊ������Ϣ
 */
int check_path(const char* path) {
	if (path == NULL)
		return -1;
	const char* to_path = "/mnt/sdcard/owncloud/admin@192.168.111.11/test.txt";
	if (strcmp(to_path, path) == 0)
		return 1;
	else
		return -1;
}

/*
 * ������ʱ�ļ�·��
 * �������磺/mnt/sdcard/owncloud/admin@192.168.111.11/test.txt
 * �����ʱ�ļ�·����/mnt/sdcard/owncloud/.tmp/.mnt_sdcard_owncloud_admin@192.168.111.11_test.txt
 */
char* create_tmpfile(const char* path) {
	int len = strlen(path);
	char *input = (char *) malloc(len + 1);
	char prepath[] = "/mnt/sdcard/owncloud/.tmp/.";
	char *output = (char *) malloc(len + strlen(prepath) + 1);

	char tmppath[] = "/mnt/sdcard/owncloud/.tmp";

	/* ������ʱĿ¼ */
	mkdir(tmppath, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);

	/* ��ʼ�� */
	memset(input, '\0', sizeof(char) * (len + 1));
	memset(output, '\0', sizeof(char) * (len + strlen(prepath) + 1));

	strcpy(input, path);
	strcat(output, prepath);

	char *p = strtok(input, "/");
	strcat(output, p);
	while ((p = strtok(NULL, "/"))) {
		printf("%s\n", p);
		strcat(output, "_");
		strcat(output, p);

	}
//	open(output, O_WRONLY | O_CREAT | O_TRUNC);
	free(input);
	return output;
}

/*
 * ����ʱ�ļ�·���ָ�Ϊ��ʵ�ļ�·��
 * ������磺/mnt/sdcard/owncloud/admin@192.168.111.11/test.txt
 * ������ʱ�ļ�·����/mnt/sdcard/owncloud/.tmp/.mnt_sdcard_owncloud_admin@192.168.111.11_test.txt
 */
char* recover_tmpfile(const char* path) {
	int len = strlen(path);
	char *input = (char *) malloc(len + 1);
	char prepath[] = "/";
	char *output = (char *) malloc(len + strlen(prepath) + 1);

	/* ��ʼ�� */
	memset(input, '\0', sizeof(char) * (len + 1));
	memset(output, '\0', sizeof(char) * (len + strlen(prepath) + 1));

	strcpy(input, path);
	strcat(output, prepath);

	char *check = strstr(input, "/mnt/sdcard/owncloud/.tmp/");
	if (check == NULL)
		return NULL;

	char *p = strtok(input, "_");
	strcat(output, "mnt");
	printf("%s\n", p);
	while ((p = strtok(NULL, "_"))) {
		strcat(output, "/");
		strcat(output, p);
	}

	free(input);
	return output;
}