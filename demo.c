#include <fcgi_stdio.h>  
#include <unistd.h>
#include <stdlib.h>  
#include <string.h>

int main() {  
    int count = 0;  
    while (FCGI_Accept() >= 0) {  
        printf("Content-type: text/html\r\n"  
                "\r\n"  
                ""  
                "FastCGI Hello!"  
                "Request number %d running on host%s "  
                "Process ID: %d"
                "query_string:%s\n", ++count, getenv("SERVER_NAME"), getpid(),getenv("QUERY_STRING"));  
        
        char* method = getenv("REQUEST_METHOD");
        if(!strcmp(method, "POST")){
            int ilen = atoi(getenv("CONTENT_LENGTH"));
            char *bufp = malloc(ilen);
            fread(bufp, ilen,1,stdin);
            printf("THE POST data is<P>%s\n",bufp);
            printf("SCRIPT_FILENAME:%s\n",getenv("SCRIPT_FILENAME"));
            char* url_name = getenv("SCRIPT_FILENAME");
            char* str = (char *)malloc(strlen(url_name));
            int len = 0;
            for(int i = strlen(url_name)-1;i >= 0;i--){
                if(url_name[i] == '/') break;
                len++;
            }
            char tmp[len-1];
            int index = 0;
            for(int i = strlen(url_name)-1;i >= 0;i--){
                if(url_name[i] == '/') break;
                tmp[index++] = url_name[i];
            }
            int len1 = strlen(tmp);
            char* p = tmp;
            char* p1 = &tmp[len1-1];
            while(p < p1){
                char temp = *p;
                *p = *p1;
                *p1 = temp;
                p++;
                p1--;
            }
            char* ans = tmp;
            printf("ans is : %s\n",ans);
            free(bufp);

        }
    }  
    return 0;  
}  
