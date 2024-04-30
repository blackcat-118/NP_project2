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
bool user_list[33];

class my_proc {
public:
    my_proc(char* cname): cname(cname), next(nullptr), arg_count(1), arg_list{cname, NULL},
    line_count(-1), pipefd{0, 1}, readfd(0), writefd(1), pid(-1), output_file{}, input_file{}{}

    virtual ~my_proc(){}
    my_proc* next;
    deque<my_proc*> prev;
    char* cname;
    int arg_count;
    char* arg_list[15];
    int line_count;
    int readfd, writefd;
    int pipefd[2];
    int pid;  // if pid doesn't execute successfully(such as killed by someone), it will be -1
    char output_file[100];
    char input_file[20];
    bool completed = false;  // whether main process takes back child pid or not
    bool err = false;
    bool p_flag = false; //normal pipe FLAG
    bool up_flag = false; //user pipe FLAG (whether this process pipe to another user's)

};
class mypipe {
public:
    mypipe(unsigned int id1, unsigned int id2, int readfd, int writefd, my_proc* p): sender_id(id1), recevier_id(id2), readfd(readfd), writefd(writefd), p(p){}
    unsigned int sender_id;
    unsigned int recevier_id;
    int readfd;
    int writefd;
    my_proc* p;
};
class user {
public:
    user(unsigned int id, string addr, int port, int sockfd): username("(no name)"), id(id), env_var({{"PATH", "bin:."}}), addr(addr), port(port), sockfd(sockfd){}
    virtual ~user() {
        for (int i = 0; i < proc.size(); i++) {
            delete(proc[i]);
        }
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
map<unsigned int, user*> users;
deque<char*> cmd;
char cmd_copy[20000];
vector<mypipe*>user_pipe;

void sig_waitchild(int signo) {
    if (signo == SIGCHLD) {
        int status;
        while(waitpid(-1,&status,WNOHANG) > 0){}
    }
    return;
}

void unicast_msg(string mode, int userid, char* msg) {

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

void broadcast_msg(string mode, int userid, char* msg) {

    string strmsg;
    if (mode == "login") {
        strmsg = "*** User \'" + users[userid]->username + "\' entered from " + users[userid]->addr + ":" + to_string(users[userid]->port) + ". ***\n";
        msg = strmsg.data();

    }
    else if (mode == "logout") {
        strmsg = "*** User \'" + users[userid]->username + "\' left. ***\n";
        msg = strmsg.data();
    }
    else if (mode == "yell") {
        strmsg = "*** " + users[userid]->username + " yelled ***: " + msg + "\n";
        msg = strmsg.data();
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
    bool flag = false;  // whether it should create a pipe
    if (p->line_count > 0) {

        for (int i = users[userid]->proc.size()-2; i >= 0; i--) {
            my_proc* p1 = proc[i];

            if (p1->line_count == p->line_count) {
                //pipe to the same line later
                //let they use one pipe
                p->pipefd[0] = p1->pipefd[0];
                p->pipefd[1] = p1->pipefd[1];
                flag = true;
                break;
            }
        }
        if (p->p_flag)
            flag = false;
        if (flag == false) {
            //cout << "create pipe" << endl;
            create_pipe(&p->pipefd[0]);
            used_pipe.push_back(p->pipefd[0]);
            used_pipe.push_back(p->pipefd[1]);
        }
    }
    int readfd, writefd;
    int wfd = 1;
    if (!p->prev.empty()) {
        readfd = p->prev.front()->pipefd[0];
        wfd = p->prev.front()->writefd;
    }
    else {
        readfd = p->readfd;  //rfd is real readfd, origin readfd is used for storing pipe reading port
    }
    writefd = p->pipefd[1];
    p->readfd = readfd;
    p->writefd = writefd;
    //cout << p->cname << readfd << writefd << endl;

    p->pid = fork();
    //cout << p->cname << " " << p->pid << endl;
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

        int fd1 = -1, fd2 = -1;
        if (strlen(p->input_file) != 0) {
            fd1 = open(p->input_file, O_RDONLY);
            read_pipe(fd1);
        }
        if (strlen(p->output_file) != 0) {
            fd2 = open(p->output_file, O_TRUNC|O_WRONLY|O_CREAT, 0644);
            write_pipe(fd2);
        }
        //cout << p->cname << endl;
        close_pipes(used_pipe);  //also close the pipe in different pipeline

        if (execvp(p->cname, p->arg_list) == -1) {
            cerr << "Unknown command: [" << p->cname << "]." << endl;
            kill_prev(p);
            if (fd1 != -1) {
                close(fd1);
            }
            if (fd2 != -1) {
                close(fd2);
            }
            exit(1);
        }
        exit(0);
    }
    else {
        //parent process
        if (readfd != 0) {

            for (int a = 0; a < used_pipe.size(); a++) {
                if (used_pipe[a] == readfd) {
                    used_pipe.erase(used_pipe.begin()+a);
                    close(readfd);
                }
            }
        }
        if (wfd != 1) {

            for (int a = 0; a < used_pipe.size(); a++) {
                if (used_pipe[a] == wfd) {
                    used_pipe.erase(used_pipe.begin()+a);
                    close(wfd);
                }
            }
        }
        if (p->up_flag == true) {
            if (writefd != 1) {

                for (int a = 0; a < used_pipe.size(); a++) {
                    if (used_pipe[a] == writefd) {
                        used_pipe.erase(used_pipe.begin()+a);
                        close(writefd);
                    }
                }
            }
        }

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
void wait_childpid(int userid) {
    int wstatus;
    bool s_flag = false;
    deque<my_proc*>& proc = users[userid]->proc;

    // this block is used for waitpid
    for (int i = 0; i < proc.size(); i++) {
        my_proc* p = proc[i];
        if (p->completed == true || p->up_flag == true)  // if this cmd pipe to another user, it should wait later
            continue;
        if (p->line_count <= 0) {  //it means p's next is ready

            my_proc* nxt = p->next;
            if (nxt == nullptr) { //if it doesn't have next
                waitpid(p->pid, &wstatus, 0);
                p->completed = true;
            }

            else {
                while (nxt) {     //if it has next
                    if (nxt->line_count > 0 ) { //it means next's next is not ready
			            break;
                    }

                    if (nxt->next == nullptr || p->line_count < -100) { //reach pipeline end
                        waitpid(p->pid, &wstatus, 0); // here still should wait
                        p->completed = true;
                        break;
                    }
                    nxt = nxt->next;
                }
            }
        }
    }
    if (s_flag)
	    usleep(20);
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
        int sender_id = -1, receiver_id = -1;

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
                    sender_id = atoi(next_cmd);
                    cmd.pop_front();

                }
                else if (next_cmd[0] == '>') {  //user write pipe
                    next_cmd[0] = ' ';
                    receiver_id = atoi(next_cmd);
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

            // check exists for sender and receiver
            if (sender_id != -1) {
                if (users.find(sender_id) == users.end()) {
                    cout <<  "*** Error: user #" << sender_id << " does not exist yet. ***" << endl;
                    strcpy(cur->input_file, "/dev/null");
                }
                else {
                    int indx = -1;
                    for (int i = 0; i < user_pipe.size(); i++) {
                        if (userid == user_pipe[i]->recevier_id && sender_id == user_pipe[i]->sender_id) {
                            cur->readfd = user_pipe[i]->readfd;
                            //cur->prev.push_back(user_pipe[i]->p);
                            //user_pipe[i]->p->next = cur;
                            user_pipe[i]->p->up_flag = false;
                            indx = i;
                            break;
                        }
                    }
                    // print pipe status
                    if (indx == -1) {
                        cout << "*** Error: the pipe #" << sender_id << "->#" << userid << " does not exist yet. ***" << endl;
                        strcpy(cur->input_file, "/dev/null");
                    }
                    else {
                        string command = cmd_copy;
                        string strmsg = "*** " + users[userid]->username + " (#" + to_string(userid) + ") just received from "
                                        + users[sender_id]->username + " (#" + to_string(sender_id) + ") by \'" + command + "\' ***\n";
                        broadcast_msg("n", userid, strmsg.data());
                        user_pipe.erase(user_pipe.begin()+indx);
                    }
                }
            }
            if (receiver_id != -1) {
                if (users.find(receiver_id) == users.end()) {
                    cout <<  "*** Error: user #" << receiver_id << " does not exist yet. ***" << endl;
                    strcpy(cur->output_file, "/dev/null");
                }
                // always the write cmd to create pipe
                else {
                    int indx = -1;
                    for (int i = 0; i < user_pipe.size(); i++) {
                        if (userid == user_pipe[i]->sender_id && receiver_id == user_pipe[i]->recevier_id) {
                            indx = i;
                            break;
                        }
                    }
                    // print user pipe status
                    if (indx != -1) {
                        cout << "*** Error: the pipe #" << userid << "->#" << receiver_id << " already exists. ***" << endl;
                        strcpy(cur->output_file, "/dev/null");
                    }
                    else {
                        cur->up_flag = true;
                        // create user pipe
                        create_pipe(&cur->pipefd[0]);
                        //cur->readfd = pipefd[0];
                        cur->writefd = cur->pipefd[1];
                        used_pipe.push_back(cur->pipefd[0]);
                        used_pipe.push_back(cur->pipefd[1]);
                        // add to user pipe list
                        mypipe* p1 = new mypipe(userid, receiver_id, cur->pipefd[0], cur->pipefd[1], cur);
                        user_pipe.push_back(p1);
                        // broadcast pipe message
                        string command = cmd_copy;
                        string strmsg = "*** " + users[userid]->username + " (#" + to_string(userid) + ") just piped \'" + command
                                        + "\' to " + users[receiver_id]->username + " (#" + to_string(receiver_id) + ") ***\n";
                        broadcast_msg("n", userid, strmsg.data());
                    }
                }
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
        do_fork(userid, cur);
        line_counter(userid);
        wait_childpid(userid);
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
                for (int i = 0; i < user_pipe.size(); i++) {
                    if (user_pipe[i]->sender_id == userid || user_pipe[i]->recevier_id == userid) {
                        close(user_pipe[i]->readfd);
                        close(user_pipe[i]->writefd);
                        user_pipe.erase(user_pipe.begin()+i);
                        i--;
                    }
                }
                broadcast_msg("logout", userid, nullptr);
                delete(users.find(userid)->second);
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
            else if (strcmp(cur_cmd, "who") == 0) {
                cout << "<ID>\t<nickname>\t<IP:port>\t<indicate me>" << endl;
                for (map<unsigned int, user*>::iterator it = users.begin(); it != users.end(); it++) {
                    cout << it->second->id << "\t" << it->second->username << "\t" << it->second->addr << ":" << it->second->port << "\t";
                    if (it->second->id == userid) {
                        cout << "<-me";
                    }
                    cout << endl;
                }
            }
            else if (strcmp(cur_cmd, "name") == 0) {
                string newname = strtok(NULL, " ");
                bool ch_flag = true;  // change name flag
                for (map<unsigned int, user*>::iterator it = users.begin(); it != users.end(); it++) {
                    if (it->second->username == newname) {
                        cout << "*** User \'" << newname << "\' already exists. ***" << endl;
                        ch_flag = false;
                        break;
                    }
                }
                if (ch_flag == true) {
                    users[userid]->username = newname;
                    string msg = "*** User from " + users[userid]->addr + ":" + to_string(users[userid]->port) + " is named \'" + newname + "\'. ***\n";
                    broadcast_msg("n", userid, msg.data());
                }
            }
            else if (strcmp(cur_cmd, "yell") == 0) {
                char* msg = strtok(NULL, "");
                broadcast_msg("yell", userid, msg);
            }
            else if (strcmp(cur_cmd, "tell") == 0) {
                int u1_id = atoi(strtok(NULL, " "));
                if (users.find(u1_id) == users.end()) {
                    cout <<  "*** Error: user #" << u1_id << " does not exist yet. ***" << endl;
                    strtok(NULL, "");
                }
                else {
                    string msg = "*** " + users[userid]->username + " told you ***: " + strtok(NULL, "") + "\n";
                    unicast_msg("tell", u1_id, msg.data());
                }

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

    signal(SIGCHLD, sig_waitchild);
    // initialization
    for (int i = 0; i < 33; i++) {
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
    serv_addr.sin_port = htons(7001);
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
            for (int i = 1; i < 33; i++) {
                if (user_list[i] == false) {
                    new_usrid = i;
                    break;
                }
            }
            if (new_usrid > 0) {
                user_list[new_usrid] = true;
                cout << "Server: add user " << new_usrid << " from " << inet_ntoa(cli_addr.sin_addr) << endl;

                user* new_usr = new user(new_usrid, inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port), ssock);
                users.insert(pair<unsigned int, user*>(new_usrid, new_usr));
                unicast_msg("welcome", new_usrid, nullptr);  // unicast welcome message
                broadcast_msg("login", new_usrid, nullptr);  // broadcast login message
                unicast_msg("start", new_usrid, nullptr);
            }
        }
        for (int i = 1; i < 33; i++) {
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



