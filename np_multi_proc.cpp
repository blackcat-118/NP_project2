using namespace std;
#include <iostream>
#include <stdio.h>
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
#include <sys/shm.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#ifndef DEBUG
#define DEBUG_BLOCK(statement)
#else
#define DEBUG_BLOCK(statement) do { statement } while (0)
#endif

#define MAX_ARGUMENTS 15
#define MAX_USER 32

#define SHMKEY1 ((key_t) 7890) /* base value for shmem key */
#define SHMKEY2 ((key_t) 7891) /* base value for shmem key */
#define SHMKEY3 ((key_t) 7892) /* base value for shmem key */
#define SHMKEY4 ((key_t) 7893) /* base value for shmem key */
//#define SEMKEY1 ((key_t) 7892) /* client semaphore key */
//#define SEMKEY2 ((key_t) 7893) /* server semaphore key */
#define PERMS 0666

#define FIFO_PATH "user_pipe/fifo"

int shm_ul, shm_usr, shm_up, shm_msg, clisem, servsem; /* shared memory and semaphore IDs */
deque<char*> cmd;
char cmd_copy[20000];
class my_proc {
public:
    my_proc(char* cname): cname(cname), next(nullptr), arg_count(1), arg_list{cname, NULL},
    line_count(-1), pipefd{0, 1}, output_file{}, input_file{}{}

    virtual ~my_proc(){}
    my_proc* next;
    deque<my_proc*> prev;
    char* cname;
    int arg_count = 1;
    char* arg_list[15];
    int line_count;
    int readfd = 0;
    int writefd = 1;
    int pipefd[2];
    int pid = -1;  // if pid doesn't execute successfully(such as killed by someone), it will be -1
    char output_file[100];
    char input_file[20];
    bool completed = false;  // whether main process takes back child pid or not
    bool err = false;
    bool p_flag = false; //normal pipe FLAG
    bool up_flag = false; //user pipe FLAG (whether this process pipe to another user's)

};

class user {
public:
    unsigned int id;
    unsigned int pid;
    char username[100];
    char addr[32];
    unsigned short port;
    int sockfd;
};

class mypipe {
public:
    unsigned int sender_id;
    unsigned int recevier_id;
    char pipepath[100];
    int status = 0;  // 0->unused; 1->write-ing; 2->read-ing
};
user *users;    //shared
char* bc_msg;  //shared
mypipe *user_pipe;   //shared
bool *user_list; //shared

vector<int> used_pipe;
deque<my_proc*> proc;
int pipe_counter = 0;

void sig_exit(int signo) {
    if (signo == SIGINT) {
        if (shmdt(user_list) < 0) {
            cerr << "unattach shared memory1 error: " << strerror(errno) << endl;
        }
        if (shmctl(shm_ul, IPC_RMID, (struct shmid_ds *) 0) < 0) {
            cerr << "remove shared memory1 error: " << strerror(errno) << endl;
        }
        if (shmdt(users) < 0) {
            cerr << "unattach shared memory2 error: " << strerror(errno) << endl;
        }
        if (shmctl(shm_usr, IPC_RMID, (struct shmid_ds *) 0) < 0) {
            cerr << "remove shared memory2 error: " << strerror(errno) << endl;
        }
        if (shmdt(user_pipe) < 0) {
            cerr << "unattach shared memory3 error: " << strerror(errno) << endl;
        }
        if (shmctl(shm_up, IPC_RMID, (struct shmid_ds *) 0) < 0) {
            cerr << "remove shared memory3 error: " << strerror(errno) << endl;
        }
        if (shmdt(bc_msg) < 0) {
            cerr << "unattach shared memory4 error: " << strerror(errno) << endl;
        }
        if (shmctl(shm_msg, IPC_RMID, (struct shmid_ds *) 0) < 0) {
            cerr << "remove shared memory4 error: " << strerror(errno) << endl;
        }
        exit(0);

    }
    return;
}

void sig_waitchild(int signo) {
    if (signo == SIGCHLD) {
        int status;
        while(waitpid(-1,&status,WNOHANG) > 0){}
    }
    return;
}

void sig_broadcast(int signo) {
    if (signo == SIGUSR1) {
        cout << bc_msg;
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
    cout << msg;

    return;
}

void broadcast_msg(string mode, int userid, char* msg) {

    string strmsg;
    if (mode == "login") {
        strmsg = "*** User \'" + string(users[userid].username) + "\' entered from " + string(users[userid].addr) + ":" + to_string(users[userid].port) + ". ***\n";
        msg = strmsg.data();

    }
    else if (mode == "logout") {
        strmsg = "*** User \'" + string(users[userid].username) + "\' left. ***\n";
        msg = strmsg.data();
    }
    else if (mode == "yell") {
        strmsg = "*** " + string(users[userid].username) + " yelled ***: " + msg + "\n";
        msg = strmsg.data();
    }
    strcpy(bc_msg, msg);
    for (int i = 1; i <= MAX_USER; i++) {
        if (user_list[i] == true) {
            kill(users[i].pid, SIGUSR1);
            //write(users[i].sockfd, msg, strlen(msg));
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


void do_fork(int userid, my_proc* p) {
    //deque<my_proc*>& proc = users[userid].proc;

    int pipefd[2];
    if (p->pid != -1) {
        return;
    }
    //cout << p->line_count << endl;
    bool flag = false;  // whether it should create a pipe
    if (p->line_count > 0) {

        for (int i = proc.size()-2; i >= 0; i--) {
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
    int readfd = 0, writefd = 1;
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
        /*if (p->up_flag == true) {
            if (writefd != 1) {

                for (int a = 0; a < used_pipe.size(); a++) {
                    if (used_pipe[a] == writefd) {
                        used_pipe.erase(used_pipe.begin()+a);
                        close(writefd);
                    }
                }
            }
        }*/

    }
    return;
}

void line_counter(int userid) {
    //deque<my_proc*>& proc = users[userid].proc;
    for (int i = 0; i < proc.size(); i++) {
        proc[i]->line_count--;
        if (proc[i]->p_flag == false && proc[i]->line_count >= 0) {
            proc[i]->line_count += pipe_counter;
        }
    }
    pipe_counter = 0;
    return;
}

void check_proc_pipe(int userid, my_proc* cur) {
    //deque<my_proc*>& proc = users[userid].proc;
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
    //deque<my_proc*>& proc = users[userid].proc;

    // this block is used for waitpid
    for (int i = 0; i < proc.size(); i++) {
        my_proc* p = proc[i];
        if (p->completed == true )  //|| p->up_flag == true if this cmd pipe to another user, it should wait later
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
    //deque<my_proc*>& proc = users[userid].proc;

    while (cmd.size()) {
        cur_cmd = cmd.front();
        cmd.pop_front();

        DEBUG_BLOCK (
                     cout << "create proc: " << cur_cmd << endl;
                     );

        my_proc* cur = new my_proc(cur_cmd);
        proc.push_back(cur);
        int sender_id = -1, receiver_id = -1;
        int read_indx = -1, write_indx = -1;
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
                    if (sender_id == userid)
                        sender_id = -1;
                    cmd.pop_front();

                }
                else if (next_cmd[0] == '>') {  //user write pipe
                    next_cmd[0] = ' ';
                    receiver_id = atoi(next_cmd);
                    if (receiver_id == userid)
                        receiver_id = -1;
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
                bool exist_flag = true;
                //if (users.find(sender_id) == users.end()) {
                if (sender_id > MAX_USER || user_list[sender_id] == false) {
                    cout <<  "*** Error: user #" << sender_id << " does not exist yet. ***" << endl;
                    strcpy(cur->input_file, "/dev/null");
                }
                else {
                    for (int i = 0; i < MAX_USER*MAX_USER; i++) {
                        if (user_pipe[i].status == 0) 
                            continue;
                        if (userid == user_pipe[i].recevier_id && sender_id == user_pipe[i].sender_id) {
                            strcpy(cur->input_file, user_pipe[i].pipepath);
                            //cur->readfd = user_pipe[i].readfd;
                            //user_pipe[i].p->up_flag = false;
                            read_indx = i;
                            break;
                        }
                    }
                    // print pipe status
                    if (read_indx == -1) {
                        cout << "*** Error: the pipe #" << sender_id << "->#" << userid << " does not exist yet. ***" << endl;
                        strcpy(cur->input_file, "/dev/null");
                    }
                    else {
                        string command = cmd_copy;
                        string strmsg = "*** " + string(users[userid].username) + " (#" + to_string(userid) + ") just received from "
                                        + string(users[sender_id].username) + " (#" + to_string(sender_id) + ") by \'" + command + "\' ***\n";
                        broadcast_msg("n", userid, strmsg.data());
                        //user_pipe[indx].used = false;
                        //user_pipe.erase(user_pipe.begin()+indx);
                    }
                }
            }
            if (receiver_id != -1) {
                //if (users.find(receiver_id) == users.end()) {
                if (receiver_id > MAX_USER || user_list[receiver_id] == false) {
                    cout <<  "*** Error: user #" << receiver_id << " does not exist yet. ***" << endl;
                    strcpy(cur->output_file, "/dev/null");
                }
                // always the write cmd to create pipe
                else {
                    int indx = -1;
                    for (int i = 0; i < MAX_USER*MAX_USER; i++) {
                        if (user_pipe[i].status == 0)
                            continue;
                        if (userid == user_pipe[i].sender_id && receiver_id == user_pipe[i].recevier_id) {
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
                        for (int i = 0; i < MAX_USER*MAX_USER; i++) {
                            if (user_pipe[i].status == 0) {
                                write_indx = i;
                                break;
                            }
                        }
                        user_pipe[write_indx].sender_id = userid;
                        user_pipe[write_indx].recevier_id = receiver_id;
                        user_pipe[write_indx].status = 1;
                        sprintf(user_pipe[write_indx].pipepath, "%s%d_%d", FIFO_PATH, sender_id, receiver_id);
                        if ( (mknod(user_pipe[write_indx].pipepath, S_IFIFO | PERMS, 0) < 0)) {
                            cerr << "Error: create fifo" << errno << endl;
                        }
                        strcpy(cur->output_file, user_pipe[write_indx].pipepath);
                        // broadcast pipe message
                        string command = cmd_copy;
                        string strmsg = "*** " + string(users[userid].username) + " (#" + to_string(userid) + ") just piped \'" + command
                                        + "\' to " + string(users[receiver_id].username) + " (#" + to_string(receiver_id) + ") ***\n";
                        broadcast_msg("n", userid, strmsg.data());
                    }
                }
            }

            if (strcmp(next_cmd, "|") == 0) { //pipe to next
                cur->line_count = 1;
                cur->p_flag = true;
                pipe_counter++;
                cmd.pop_front();
            }
            else if (strcmp(next_cmd, "!") == 0) {
                cur->err = true;
                cur->line_count = 1;
                cur->p_flag = true;
                pipe_counter++;
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
        
        if (read_indx != -1) {
            sleep(1);
            if (user_pipe[read_indx].status != 2)
                cout << "previous process can't complete writing immediately." << endl;
        }
        if (write_indx != -1) {
            sleep(1);
            if (user_pipe[write_indx].status != 0)
                cout << "previous process can't complete reading immediately." << endl;
        }
        check_proc_pipe(userid, cur);
        do_fork(userid, cur);
        line_counter(userid);
        wait_childpid(userid);
        if (read_indx != -1) {
            user_pipe[read_indx].status = 0;
            if (unlink(user_pipe[read_indx].pipepath) < 0)
                cout << "client: can't unlink" << endl;
        }
        if (write_indx != -1) {
            user_pipe[write_indx].status = 2;
        }
        
    }
    return;
}

void Input(int userid) {
    char* cur_cmd;
    setenv("PATH", "bin:.", 1);
    //initial PATH environment varible (this does not need in concurrent-oriented server)
    //for (map<string, string>::iterator it = users[userid].env_var.begin(); it != users[userid].env_var.end(); it++) {
    //    setenv(it->first.data(), it->second.data(), 1);
    //}
    while (1) {
        char lined_cmd[20000];
        cout << "% ";
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
                user* usr = &users[userid];
                close(usr->sockfd);

                user_list[userid] = false;
                for (int i = 0; i < MAX_USER*MAX_USER; i++) {
                    if (user_pipe[i].status == 0)
                        continue;
                    if (user_pipe[i].sender_id == userid || user_pipe[i].recevier_id == userid) {
                        //close(user_pipe[i].readfd);
                        //close(user_pipe[i].writefd);
                        user_pipe[i].status = 0;
                        if (unlink(user_pipe[i].pipepath) < 0)
                            cout << "client: can't unlink" << endl;
                    }
                }
                broadcast_msg("logout", userid, nullptr);

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

                setenv(arg1, arg2, 1);
                line_counter(userid);

            }
            else if (strcmp(cur_cmd, "who") == 0) {
                cout << "<ID>\t<nickname>\t<IP:port>\t<indicate me>" << endl;
                //for (map<unsigned int, user*>::iterator it = users.begin(); it != users.end(); it++) {
                for (int i = 1; i <= MAX_USER; i++) {
                    if (user_list[i] == false)
                        continue;
                    cout << i << "\t" << users[i].username << "\t" << users[i].addr << ":" << users[i].port << "\t";
                    //cout << it->second->id << "\t" << it->second->username << "\t" << it->second->addr << ":" << it->second->port << "\t";
                    if (i == userid) {
                        cout << "<-me";
                    }
                    cout << endl;
                }
            }
            else if (strcmp(cur_cmd, "name") == 0) {
                string newname = strtok(NULL, " ");
                bool ch_flag = true;  // change name flag
                //for (map<unsigned int, user*>::iterator it = users.begin(); it != users.end(); it++) {
                for (int i = 1; i <= MAX_USER; i++) {
                    if (user_list[i] == false)
                        continue;
                    if (strcmp(users[i].username, newname.data()) == 0) {
                        cout << "*** User \'" << newname << "\' already exists. ***" << endl;
                        ch_flag = false;
                        break;
                    }
                }
                if (ch_flag == true) {
                    strcpy(users[userid].username, newname.data());
                    string msg = "*** User from " + string(users[userid].addr) + ":" + to_string(users[userid].port) + " is named \'" + newname + "\'. ***\n";
                    broadcast_msg("n", userid, msg.data());
                }
            }
            else if (strcmp(cur_cmd, "yell") == 0) {
                char* msg = strtok(NULL, "");
                broadcast_msg("yell", userid, msg);
            }
            else if (strcmp(cur_cmd, "tell") == 0) {
                int u1_id = atoi(strtok(NULL, " "));
                //if (users.find(u1_id) == users.end()) {
                if (u1_id > MAX_USER || user_list[u1_id] == false) {
                    cout <<  "*** Error: user #" << u1_id << " does not exist yet. ***" << endl;
                    strtok(NULL, "");
                }
                else {
                    string msg = "*** " + string(users[userid].username) + " told you ***: " + strtok(NULL, "") + "\n";
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
        //cout << "% " << flush;
    }
    return;
}

int main(int argc, char** argv, char** envp) {
    signal(SIGINT, sig_exit);
    signal(SIGCHLD, sig_waitchild);
    signal(SIGUSR1, sig_broadcast);
    // create shared memory
    if ( (shm_ul = shmget(SHMKEY1, sizeof(bool)*33, PERMS|IPC_CREAT)) < 0) {
        cerr << "create shared memory1 error: " << strerror(errno) << endl;
        return 1;
    }
    if ( ( user_list = (bool *) shmat(shm_ul, NULL, 0)) == (bool*)-1){
        cerr << "attach shared memory1 error: " << strerror(errno) << endl;
        return 1;
    }
    if ( (shm_usr = shmget(SHMKEY2, sizeof(user)*33, PERMS|IPC_CREAT)) < 0) {
        cerr << "create shared memory2 error: " << strerror(errno) << endl;
        return 1;
    }
    if ( ( users = (user*) shmat(shm_usr, NULL, 0)) == (user*)-1){
        cerr << "attach shared memory2 error: " << strerror(errno) << endl;
        return 1;
    }
    if ( (shm_up = shmget(SHMKEY3, sizeof(mypipe)*33*33, PERMS|IPC_CREAT)) < 0) {
        cerr << "create shared memory3 error: " << strerror(errno) << endl;
        return 1;
    }
    if ( ( user_pipe = (mypipe *) shmat(shm_up, NULL, 0)) == (mypipe*)-1){
        cerr << "attach shared memory3 error: " << strerror(errno) << endl;
        return 1;
    }
    if ( (shm_msg = shmget(SHMKEY4, sizeof(char)*10000, PERMS|IPC_CREAT)) < 0) {
        cerr << "create shared memory4 error: " << strerror(errno) << endl;
        return 1;
    }
    if ( ( bc_msg = (char*) shmat(shm_msg, NULL, 0)) == (char*)-1){
        cerr << "attach shared memory4 error: " << strerror(errno) << endl;
        return 1;
    }
    // initialization
    for (int i = 1; i < MAX_USER; i++) {
        // user status
        user_list[i] = false;
        users[i].sockfd = -1;
    }
    int msock, ssock, childpid;
    struct sockaddr_in cli_addr, serv_addr;
    int reuse_flag = 1;

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
    listen(msock, 1);
    cout << "Server: start listen" << endl;
    while (true) {
        socklen_t clilen = sizeof(cli_addr);
        ssock = accept(msock, (struct sockaddr*) &cli_addr, &clilen);
        cout << "Server: accept a client" << endl;
        if (ssock < 0) {
            cerr << "Server accept error: " << strerror(errno) << endl;
            continue;
        }
        int new_usrid = -1;
        for (int i = 1; i <= MAX_USER; i++) {
            if (user_list[i] == false) {
                new_usrid = i;
                break;
            }
        }
        if (new_usrid > 0) {
            user_list[new_usrid] = true;
            cout << "Server: add user " << new_usrid << " from " << inet_ntoa(cli_addr.sin_addr) << endl;
            users[new_usrid].id = new_usrid;
            strcpy(users[new_usrid].username, "(no name)");
            strcpy(users[new_usrid].addr, inet_ntoa(cli_addr.sin_addr));
            users[new_usrid].port = ntohs(cli_addr.sin_port);
            users[new_usrid].sockfd = ssock;
        }
        else {
            cout << "Server: reach maximum of users" << endl;
            sleep(1);
            continue;
        }
        childpid = fork();
        if (childpid < 0) {
            cerr << "Server fork error: " << strerror(errno) << endl;
        }
        else if (childpid == 0) {
            // child process
            // pipe input and output to client
            close(0);
            dup(ssock);
            close(1);
            dup(ssock);
            close(2);
            dup(ssock);

            close(msock);
            unicast_msg("welcome", new_usrid, nullptr);  // unicast welcome message
            broadcast_msg("login", new_usrid, nullptr);  // broadcast login message
            //unicast_msg("start", new_usrid, nullptr);
            Input(new_usrid);
            exit(0);
        }
        else {
            // parent process
            users[new_usrid].pid = childpid;
            close(ssock);
        }
    }
    
    return 0;
}



