#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <assert.h>
#include <stack>
#include <algorithm>

#define clean_errno() (errno == 0 ? "None" : strerror(errno))
#define log_error(M, ...) fprintf(stderr, "[ERROR] (%s:%d: errno: %s) " M "\n", __FILE__, __LINE__, clean_errno(), ##__VA_ARGS__)
#define assertf(A, M, ...) if(!(A)) {log_error(M, ##__VA_ARGS__); assert(A); }

using namespace std;

string getShortName(string name){
	size_t noParan = name.rfind('(');
	size_t found0 = name.rfind('.', noParan - 1);
	if(found0 == string::npos)	return name.substr(0, noParan);
	size_t found = name.rfind('.', found0 - 1);
	if(found == string::npos)	return name.substr(0, noParan);
	return name.substr(found + 1, noParan - found - 1);
}

class Call{
public:
	Call(long is, string ir, string in) : start(is), end(-1), duration(-1), ret(ir), name(in) {
		shortname = getShortName(name);
	};
	void logEnd(long ts){
		end = ts;
		duration = end - start;
		assertf(duration >= 0, "End should be greater than Start");
	}

	bool shouldPrint() {return duration > 0;};

	void printTo(ostream& os){
			// os << "{ \"pid\": 1, \"tid\": 1, \"ts\":" << start/1000.0 <<", \"dur\":" << duration/1000.0 << ", \"ph\":\"X\", \"name\":\"" << name << "\", \"args\":{ \"return type\":\"" << ret << "\" } }";
			// os << "{ \"pid\": 1, \"tid\": 1, \"ts\":" << start/1000.0 <<", \"dur\":" << duration/1000.0 << ", \"ph\":\"X\", \"name\":\"" << shortname<< "\" }";
			os << fixed << "{ \"pid\": 1, \"tid\": 1, \"ts\":" << start/1000.0 <<", \"dur\":" << duration/1000.0 << ", \"ph\":\"X\", \"name\":\""  << name<< "\" }";
	}
private:
	long start;
	long end;
	long duration;
	string ret;
	string name;
	string shortname;
};


int main(){
	ifstream inFile("log.dat");
	string line;
	long ts;
	char delim;
	string retType, name;
	bool comma = false;

	ofstream outFile("log.dat.json");
	outFile << "{" << endl << "\"traceEvents\": [" << endl;

	stack<Call> callStack;
	cout << "reading trace file..." << endl;
	while (getline(inFile, line)){
		istringstream iss(line);
		iss >> ts >> delim >> retType;
		// if(ts > 3600000000)	break;
		if(retType == "POP"){
			// cout << "ts:" << ts << ", delim:" << delim << ", type:" << retType << endl;
			// assertf(callStack.size() > 0, "cannot pop empty stack");
			if(callStack.size() > 0){
				Call curCall = callStack.top();
				curCall.logEnd(ts);
				callStack.pop();
				if(!curCall.shouldPrint()){
					continue;
				}
				if(comma){
					outFile << "," << endl;
				} else {
					comma = true;
				}
				curCall.printTo(outFile);
			}
		} else {
			iss >> name;
			// string shortname = getShortName(name);
			// cout << shortname << endl;
			// replace( name.begin(), name.end(), '$', ':');
			// replace( retType.begin(), retType.end(), '$', ':');
			Call newCall(ts, retType, name);
			// cout << "ts:" << ts << ", delim:" << delim << ", type:" << retType << ", name:" << name << endl;
			callStack.push(newCall);
		}
	}
	inFile.close();

	cout << "dumping remaining stack..." << endl;
	// ts = 3600000000;
	while(callStack.size() > 0){
		Call curCall = callStack.top();
		curCall.logEnd(ts);
		callStack.pop();
		if(!curCall.shouldPrint())	continue;
		if(comma){
			outFile << "," << endl;
		} else {
			comma = true;
		}
		curCall.printTo(outFile);
	}

	ifstream timerIn("log.dat.timer");

  long wall_ts = -1, cpu_ts = -1, maj_pf = -1, min_pf = -1, ctx_switch = -1;
  long prev_wall_ts = -1, prev_cpu_ts = -1, prev_maj_pf = -1, prev_min_pf = -1, prev_ctx_switch = -1;
  if(getline(timerIn, line)){
  	istringstream iss(line);
  	iss >> prev_wall_ts >> delim >> prev_cpu_ts >> delim >> prev_maj_pf >> delim >>prev_min_pf >> delim >> prev_ctx_switch;
  }
	while (getline(timerIn, line)){
		istringstream iss(line);
		iss >> wall_ts >> delim >> cpu_ts >> delim >> maj_pf >> delim >> min_pf >> delim >> ctx_switch;
		long diff_wall_ts = wall_ts - prev_wall_ts;
		long diff_cpu_ts = cpu_ts - prev_cpu_ts;
		long diff_maj_pf = maj_pf - prev_maj_pf;
		long diff_min_pf = min_pf - prev_min_pf;
		long diff_ctx_switch = ctx_switch - prev_ctx_switch;
		double usage = diff_cpu_ts * 1.0 / diff_wall_ts;
		if(comma){
			outFile << "," << endl;
		} else {
			comma = true;
		}
		outFile << "{ \"pid\": 1, \"name\": \"cpuUsage\", \"ph\":\"C\", \"ts\":\"" << wall_ts/1000.0 << "\", \"args\":{ \"usage\":\"" << usage << "\" } }";
		outFile << "," << endl;
		outFile << "{ \"pid\": 1, \"name\": \"pageFault\", \"ph\":\"C\", \"ts\":\"" << wall_ts/1000.0 << "\", \"args\":{ \"major\":\"" << diff_maj_pf << "\", \"minor\":\"" << diff_min_pf << "\" } }";
		outFile << "," << endl;
		outFile << "{ \"pid\": 1, \"name\": \"context switches\", \"ph\":\"C\", \"ts\":\"" << wall_ts/1000.0 << "\", \"args\":{ \"ctx_switch\":\"" << diff_ctx_switch << "\" } }";
		prev_wall_ts = wall_ts;
		prev_cpu_ts = cpu_ts;
		prev_min_pf = min_pf;
		prev_maj_pf = maj_pf;
		prev_ctx_switch = ctx_switch;
	}


	outFile << endl << "]," << endl << "\"displayTimeUnit\":\"ns\"," << endl << "\"meta_user\": \"uber\"," << endl <<  "\"meta_cpu\": \"2\"" << endl << "}" << endl;
	outFile.close();


	return 0;
}

