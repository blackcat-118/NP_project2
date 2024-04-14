using namespace std;
#include <iostream>
#include <cerrno>
#include <cstring>
#include <string>
#include <deque>
#include <vector>
#include <map>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#ifndef DEBUG
#define DEBUG_BLOCK(statement)
#else
#define DEBUG_BLOCK(statement) do { statement } while (0)
#endif

#define MAX_ARGUMENTS 15

int nfds;
fd_set rfds;  // read file descriptor set
fd_set afds;  // active file descriptor set
int in_fd, out_fd, err_fd;
bool user_list[32];

class my_proc {
public:
    my_proc(char* cname): cname(cname), next(nullptr), arg_count(1), arg_list{cname, NULL},
    line_count(-1), readfd(0), writefd(1), completed(false), pid(-1), output_file{}, err(false), p_flag(false){}
    my_proc(char* cname, int readfd, int writefd): cname(cname), next(nullptr), arg_count(1), arg_list{cname, NULL},
    line_count(-1), readfd(readfd), writefd(writefd), completed(false), pid(-1), output_file{}, err(false), p_flag(false){}

    virtual ~my_proc(){}
    my_proc* next;
    deque<my_proc*> prev;
    char* cname;
    int arg_count;
    char* arg_list[15];
    int line_count;
    int readfd;
    int writefd;
    bool completed;  // whether main process takes back child pid or not
    int pid;  // if pid doesn't execute successfully(such as killed by someone), it will be -1
    char output_file[100];
    bool err;
    bool p_flag; //normal pipe FLAG

};
class user{
public:
    user(unsigned int id, string addr_ch, int port, int sockfd): username("(no name)"), id(id), env_var({{"PATH", "bin:."}}), port(port), sockfd(sockfd){}
    virtual ~user() {
        for (int i = 0; i < proc.size(); i++) {
            delete(proc[i]);
        }
        /*for (map<string, string>::iterator it = env_var.begin(); it != env_var.end(); it++) {
            free(it->first);
            free(it->second);
        }*/
    }
    unsigned int id;
    string username;
    map<string, string> env_var;
    string addr;
    unsigned short port;
    int sockfd;
    deque<my_proc*> proc;
    int proc_indx = 0;
    int pipe_counter = 0;
};
map<unsigned int,user*> users;
deque<char*> cmd;
char cmd_copy[20000];

//deque<my_proc*> proc;

void unicast_msg(string mode, int userid) {

    char* msg;
    string strmsg;
    if (mode == "welcome") {
        strmsg = "****************************************\n** Welcome to the information server. **\n****************************************\n";
        msg = strmsg.data();
    }
    if (mode == "start") {
        strmsg = "% ";
        msg = strmsg.data();
    }
    write(users[userid]->sockfd, msg, strlen(msg));

    return;
}

void broadcast_msg(string mode, int userid) {
    char* msg;
    string strmsg;
    if (mode == "login") {
        strmsg = "*** User \'" + users[userid]->username + "\' entered from " + users[userid]->addr + ":" + to_string(users[userid]->port) + ". ***\n";
        msg = strmsg.data();

    }
    else if (mode == "logout") {
        strmsg = "*** User \'" + users[userid]->username + "\' left. ***\n";
        msg = strmsg.data();
    }
    else if (mode == "receive") {
        string command = cmd_copy;
        strmsg = "*** " + users[userid]->username + " (#" + userid + ") just received from " +
    }

    for (int i = 1; i < 32; i++) {
        if (user_list[i] == true) {
            write(users[i]->sockfd, msg, strlen(msg));
        }

    }
    return;
}

void create_pipe(int* pipefd) {

    if (pipe(pipefd) < 0) {
        cerr << "pipe error" << endl;
    }
    return;
}

void read_pipe(int readfd) {

    close(0);
    dup(readfd);
    return;
}
void write_pipe(int writefd) {

    close(1);
    dup(writefd);
    return;
}
void wrerr_pipe(int writefd) {

    close(1);
    close(2);
    dup(writefd);
    dup(writefd);
    return;
}

void close_pipes(vector<int> fd) {

    for (int i = 0; i < fd.size(); i++) {
        //cout << "close: " << fd[i] << endl;
        close(fd[i]);
    }

    return;
}

void kill_prev(my_proc* p) {
    for (int i = 0; i < p->prev.size(); i++) {
        my_proc* p1 = p->prev[i];
        //p1->completed = true;
        if (p1->pid != -1) {
            kill(p1->pid, 9);
	    p1->pid = -1;
        }
        kill_prev(p1);
    }
    return;
}
vector<int> used_pipe;

void do_fork(int userid, my_proc* p) {
    deque<my_proc*>& proc = users[userid]->proc;

    int pipefd[2];
    if (p->pid != -1) {
        return;
    }
    //cout << p->line_count << endl;
    bool flag = false;
    if (p->line_count > 0) {

        for (int i = 0; i < users[userid]->proc_indx; i++) {
            my_proc* p1 = proc[i];

            if (p1->line_count == p->line_count) {
                //pipe to the same line later
                //let they use one pipe
                p->readfd = p1->readfd;
                p->writefd = p1->writefd;
                flag = true;
                break;
            }
        }
        if (p->p_flag)
            flag = false;
        if (flag == false) {
            //cout << "create pipe" << endl;
            create_pipe(&pipefd[0]);
            p->readfd = pipefd[0];
            p->writefd = pipefd[1];
            used_pipe.push_back(pipefd[0]);
            used_pipe.push_back(pipefd[1]);
        }
    }
    int readfd, writefd;
    if (!p->prev.empty()) {
        readfd = p->prev.front()->readfd;
    }
    else {
        readfd = p->readfd;
    }
    writefd = p->writefd;
    //cout << p->cname << readfd << writefd << endl;
    if (flag) { // merge pipe
        usleep(10000);
    }
    p->pid = fork();
    if (p->pid == -1) {
        cerr << "can't fork" << endl;
    }
    else if (p->pid == 0) {
        //child process
        if (readfd != 0) {
            read_pipe(readfd);
        }
        if (p->err) {
            wrerr_pipe(writefd);
        }
        else if (writefd != 1) {
            write_pipe(writefd);
        }

        int fd = 0;
        if (strlen(p->output_file) != 0) {
            fd = open(p->output_file, O_TRUNC|O_WRONLY|O_CREAT, 0644);
            write_pipe(fd);
        }
        //cout << p->cname << endl;
        close_pipes(used_pipe);  //also close the pipe in different pipeline

        if (execvp(p->cname, p->arg_list) == -1) {
            cerr << "Unknown command: [" << p->cname << "]." << endl;
            kill_prev(p);
            if (fd != 0) {
                close(fd);
            }
            exit(1);
        }
        if (fd != 0) {
            close(fd);
        }
        exit(0);
    }
    return;
}

void line_counter(int userid) {
    deque<my_proc*>& proc = users[userid]->proc;
    for (int i = 0; i < proc.size(); i++) {
        proc[i]->line_count--;
        if (proc[i]->p_flag == false && proc[i]->line_count >= 0) {
            proc[i]->line_count += users[userid]->pipe_counter;
        }
    }
    users[userid]->pipe_counter = 0;
    return;
}

void check_proc_pipe(int userid, my_proc* cur) {
    deque<my_proc*>& proc = users[userid]->proc;
    // check whether has a process is the current one's prev
    for (int i = 0; i < proc.size()-1; i++) {

        if (proc[i]->line_count == 0 ) { // it should pipe with this one
            cur->prev.push_back(proc[i]);
            proc[i]->next = cur;
        }

    }
    return;
}
void exec_cmd(int userid) {
    int wstatus;
    bool s_flag = false;
    deque<my_proc*>& proc = users[userid]->proc;

    for (users[userid]->proc_indx; users[userid]->proc_indx < proc.size(); users[userid]->proc_indx++) {
        my_proc* p = proc[users[userid]->proc_indx];
        //cout << i << " ";
        if (p->completed == true || p->pid != -1)
            continue;

        do_fork(userid, p);
	if (p->line_count > 0)
	    s_flag = true;

    }
    // this block is used for waitpid
    for (int i = 0; i < proc.size(); i++) {
        my_proc* p = proc[i];
        if (p->completed == true)
            continue;
        if (p->line_count <= 0) {  //it means p's next is ready
            //used_pipe.clear();

            my_proc* nxt = p->next;
            if (nxt == nullptr) { //if it doesn't have next
                if (waitpid(p->pid, &wstatus, 0) == -1) {
                    cerr << "failed to wait for child" << endl;
                }
                else {
                    p->completed = true;
                }
            }
            else {
                while (nxt) {     //if it has next
                    //my_proc* p1 = p;
                    if (nxt->line_count > 0 ) { //it means next's next is not ready
			break;
                    }

                    if (nxt->next == nullptr || p->line_count < -100) { //reach pipeline end
                        if (p->readfd != 0) {
                            close(p->readfd);
                        }
                        if (p->writefd != 1) {
                            close(p->writefd);
                        }
                        for (int a = 0; a < used_pipe.size(); a++) {
                            if (used_pipe[a] == p->readfd || used_pipe[a] == p->writefd) {
                                used_pipe.erase(used_pipe.begin()+a);
                                a--;
                            }
                        }
                        if (waitpid(p->pid, &wstatus, 0) == -1) {
                            cerr << "failed to wait for child" << endl;
                        }
                        else {
                            p->completed = true;
                        }
                        break;
                    }
                    nxt = nxt->next;
                }
            }
        }
    }
    if (s_flag)
	    usleep(20000);
    return;
}

void read_cmd(int userid) {
    char* cur_cmd;
    char* next_cmd;
    deque<my_proc*>& proc = users[userid]->proc;

    while (cmd.size()) {
        cur_cmd = cmd.front();
        cmd.pop_front();

        DEBUG_BLOCK (
                     cout << "create proc: " << cur_cmd << endl;
                     );

        my_proc* cur = new my_proc(cur_cmd);
        proc.push_back(cur);


        if (cmd.size() != 0) {  //this block is used for reading pipes or arguments
            next_cmd = cmd.front();
             while (next_cmd[0] != '|' && next_cmd[0] != '!') {  //check whether next is the current command's argument or file redirection
                if (strcmp(next_cmd, ">") == 0) {  //file redirection
                    cmd.pop_front();
                    if (cmd.size() == 0) {
                        cerr << "File redirection need a filename after > operator" << endl;
                        break;
                    }
                    next_cmd = cmd.front();
                    strcpy(cur->output_file, next_cmd);
                    cmd.pop_front();
                }
                else if (next_cmd[0] == '<') {  //user read pipe
                    next_cmd[0] = ' ';
                    int uid = atoi(next_cmd);
                    if (users.find(uid) == users.end()) {
                        cout <<  "*** Error: user #" << uid << " does not exist yet. ***" << endl;
                    }
                    cur->readfd = users[uid]->sockfd;
                    cmd.pop_front();
                }
                else if (next_cmd[0] == '>') {  //user write pipe
                    next_cmd[0] = ' ';
                    int uid = atoi(next_cmd);
                    if (users.find(uid) == users.end()) {
                        cout <<  "*** Error: user #" << uid << " does not exist yet. ***" << endl;
                    }
                    cur->writefd = users[uid]->sockfd;
                    cmd.pop_front();
                }
                else {   // command arguments
                    if (cur->arg_count >= MAX_ARGUMENTS-1) {   // last one is NULL
                        cerr << "reach argument number limit for command:" << cur_cmd << endl;
                        cmd.pop_front();
                    }
                    cur->arg_list[cur->arg_count++] = strdup(next_cmd);
                    cur->arg_list[cur->arg_count] = NULL;
                    DEBUG_BLOCK (
                             cout << "read argument: " << next_cmd << endl;
                             );
                    cmd.pop_front();
                }

                if (cmd.size() == 0) {
                    break;
                }
                next_cmd = cmd.front();
            }
            if (strcmp(next_cmd, "|") == 0) { //pipe to next
                cur->line_count = 1;
                cur->p_flag = true;
                users[userid]->pipe_counter++;
                cmd.pop_front();
            }
            else if (strcmp(next_cmd, "!") == 0) {
                cur->err = true;
                cur->line_count = 1;
                cur->p_flag = true;
                users[userid]->pipe_counter++;
                cmd.pop_front();
            }
            else if (next_cmd[0] == '|') {  //number pipe
                next_cmd[0] = ' ';
                cur->line_count = atoi(next_cmd);

                DEBUG_BLOCK (
                             cout << "number pipe of this command: " << cur->line_count << endl;
                );
                cmd.pop_front();
            }
            else if (next_cmd[0] == '!') {  //number pipe
                next_cmd[0] = ' ';
                cur->err = true;
                cur->line_count = atoi(next_cmd);

                DEBUG_BLOCK (
                             cout << "number pipe of this command: " << cur->line_count << endl;
                );
                cmd.pop_front();
            }
        }
        check_proc_pipe(userid, cur);
        exec_cmd(userid);
        line_counter(userid);
    }
    return;
}

void Input(int userid) {
    char* cur_cmd;

    //initial PATH environment varible
    for (map<string, string>::iterator it = users[userid]->env_var.begin(); it != users[userid]->env_var.end(); it++) {
        setenv(it->first.data(), it->second.data(), 1);
    }

    while (1) {
        char lined_cmd[20000];

        // get one line command
        if (!cin.getline(lined_cmd, 20000)) {
           break;
        }
        if (strlen(lined_cmd) && lined_cmd[strlen(lined_cmd)-1] == '\r') {
            lined_cmd[strlen(lined_cmd)-1] = '\0';
        }
        strcpy(cmd_copy, lined_cmd);

        cur_cmd = strtok(lined_cmd, " ");
        if (cur_cmd == NULL)
            continue;
        //cout << strcmp(cur_cmd, "exit") << endl;
        do {

            //built-in command
            if (strcmp(cur_cmd, "exit") == 0) {
                user* usr = users[userid];
                close(usr->sockfd);
                FD_CLR(usr->sockfd, &afds);

                user_list[userid] = false;
                broadcast_msg("logout", userid);
                users.erase(users.find(userid));

                return;
            }

            else if (strcmp(cur_cmd, "printenv") == 0) {
                char* arg = strtok(NULL, " ");
                if (cur_cmd == NULL) {
                    cerr << "error: need arguments for printenv" << endl;
                    break;
                }
                char* env = getenv(arg);
                if (env != NULL) {
                    cout << env << endl;
                }
                line_counter(userid);

            }
            else if (strcmp(cur_cmd, "setenv") == 0) {
                char* arg1 = strtok(NULL, " ");

                if (arg1 == NULL) {
                    cerr << "error: need arguments for setenv" << endl;
                    break;
                }
                char* arg2 = strtok(NULL, " ");
                if (arg2 == NULL) {
                    cerr << "error: need arguments for setenv" << endl;
                    break;
                }
                //cout << "setenv " << arg1 << " " << arg2 << endl;
                if (users[userid]->env_var.find(arg1) != users[userid]->env_var.end()) {
                    users[userid]->env_var.find(arg1)->second = arg2;
                }
                else {
                    users[userid]->env_var.insert(pair<string, string>(arg1, arg2));
                }
                setenv(arg1, arg2, 1);
                line_counter(userid);

            }
            else {
                char* s = (char*)malloc(sizeof(cur_cmd));
                strcpy(s, cur_cmd);
                cmd.push_back(s);
            }
            cur_cmd = strtok(NULL, " ");

        }while (cur_cmd != NULL);

        read_cmd(userid);
        cout << "% " << flush;
        break;
    }

    return;
}

int main(int argc, char** argv, char** envp) {
    int msock;
    int reuse_flag = 1;
    struct sockaddr_in serv_addr;

    // initialization
    for (int i = 0; i < 32; i++) {
        // user status
        user_list[i] = false;
    }
    in_fd = dup(0);
    out_fd = dup(1);
    err_fd = dup(2);

    msock = socket(AF_INET, SOCK_STREAM, 6);
    // set reuse socket option
    if (setsockopt(msock, SOL_SOCKET, SO_REUSEADDR, &reuse_flag, sizeof(int)) < 0) {
        cerr << "set socket option error: " << strerror(errno) << endl;
        return 1;
    }

    if (msock < 0) {
        cerr << "cannot open stream socket: " << strerror(errno) << endl;
        return 1;
    }
    bzero((char*) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(7000);
    if (bind(msock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
        cerr << "Server can't bind local address: " << strerror(errno) << endl;
        return 1;
    }
    listen(msock, 32);
    cout << "Server: start listen" << endl;

    nfds = getdtablesize();
    FD_ZERO(&afds);
    FD_SET(msock, &afds);

    while (true) {
        memcpy(&rfds, &afds, sizeof(rfds));

        if (select(nfds, &rfds, (fd_set*)0, (fd_set*)0, (struct timeval*)0) < 0) {
            cerr << "Server select error: " << strerror(errno) << endl;
            continue;
        }
        if (FD_ISSET(msock, &rfds)) {
            int ssock;
            struct sockaddr_in cli_addr;
            socklen_t clilen = sizeof(cli_addr);

            ssock = accept(msock, (struct sockaddr*) &cli_addr, &clilen);
            if (ssock < 0) {
                cerr << "Server accept error: " << strerror(errno) << endl;
                continue;
            }
            FD_SET(ssock, &afds);
            // create a new user
            int new_usrid = -1;
            for (int i = 1; i < 32; i++) {
                if (user_list[i] == false) {
                    new_usrid = i;
                    break;
                }
            }
            if (new_usrid > 0) {
                user_list[new_usrid] = true;
                cout << "Server: add user " << new_usrid << endl;
                user* new_usr = new user(new_usrid, inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port), ssock);
                users.insert(pair<unsigned int, user*>(new_usrid, new_usr));
                unicast_msg("welcome", new_usrid);  // unicast welcome message
                broadcast_msg("login", new_usrid);  // broadcast login message
                unicast_msg("start", new_usrid);
            }
        }
        for (int i = 1; i < 32; i++) {
            if (user_list[i] == false)
                continue;
            int fd = users[i]->sockfd;

            if (FD_ISSET(fd, &rfds)) {
                cout << "Server: switch to user " << users[i]->id << endl;

                // handle input/output/error message streams
                close(0);
                dup(fd);
                close(1);
                dup(fd);
                close(2);
                dup(fd);

                Input(users[i]->id);

                // restore stdin/stdout/stderr
                close(0);
                dup(in_fd);
                close(1);
                dup(out_fd);
                close(2);
                dup(err_fd);

            }
        }

    }


    return 0;
}



