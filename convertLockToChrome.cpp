#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <assert.h>
#include <stack>
#include <algorithm>
#include <map>
#include <vector>

#define clean_errno() (errno == 0 ? "None" : strerror(errno))
#define log_error(M, ...) fprintf(stderr, "[ERROR] (%s:%d: errno: %s) " M "\n", __FILE__, __LINE__, clean_errno(), ##__VA_ARGS__)
#define assertf(A, M, ...) if(!(A)) {log_error(M, ##__VA_ARGS__); assert(A); }
#define MACROSTR(k) #k

using namespace std;

// enum Action{
// 	LOCK_ACQUIRE,
// 	LOCK_GET,
// 	LOCK_INFLATE,
// };

// enum Type{
// 	THIN,
// 	FAT,
// 	CONTENTION, 	// contention inflate
// 	RECURSION,
// 	HASH
// };

// std::istream& operator >> (std::istream& i, Action& action)
// {
// 	std::string value;
// 	if (i >> value) {
// 		if (value == "LOCK_ACQUIRE") {
// 			action = LOCK_ACQUIRE;
// 		}
// 		else if (value == "LOCK_GET") {
// 			action = LOCK_GET;
// 		}
// 		else if (value == "LOCK_INFLATE") {
// 			action = LOCK_INFLATE;
// 		}
// 		else {
// 			assertf(false, "unsupported Action");
// 		}
// 	}
// 	return i;
// }

// std::istream& operator >> (std::istream& i, Type& type)
// {
// 	std::string value;
// 	if (i >> value) {
// 		if (value == "THIN") {
// 			type = THIN;
// 		}
// 		else if (value == "FAT") {
// 			type = FAT;
// 		}
// 		else if (value == "CONTENTION") {
// 			type = CONTENTION;
// 		}
// 		else if (value == "RECURSION") {
// 			type = RECURSION;
// 		}
// 		else if (value == "HASH") {
// 			type = HASH;
// 		}
// 		else {
// 			assertf(false, "unsupported Type");
// 		}
// 	}
// 	return i;
// }

class Entry{
private:
	long ts;
	int tid;
	// Action action;
	// Type type;
	string action;
	string type;
	string obj;
	int owner_tid;

public:
	// Entry(long its, int itid, Action iaction, Type itype, long iobj, int iowner) : ts(its), tid(itid), action(iaction), type(itype), obj(iobj), owner_tid(iowner) {}
	Entry(long its, int itid, string iaction, string itype, string iobj, int iowner) : ts(its), tid(itid), action(iaction), type(itype), obj(iobj), owner_tid(iowner) {}
	void pretty_print(ostream& os){
		string seperator = ", ";
		os << ts << seperator << tid << seperator << action << seperator << type << seperator << hex << obj << dec << seperator << owner_tid << endl;
	}
};

enum State{
	IDLE,
	WAIT_THIN,		// waiting for thin lock
	WAIT_FAT,			// waiting for fat lock
	WAIT 					// wait from object.wait()
};


int main(int argc, char *argv[]){
	string pids = argv[1];
	string filename = pids + ".lock";
	int pid = stoi(pids);
	int processed_tid = -1;
	ifstream inFile(filename);
	bool comma = false;
	State s = IDLE;
	string line;
	string ds, action, type;
	string obj, enter, exit;
	int tid, owner_tid;
	long ts, duration;
	char dc;
	string retType, name, location;

	ofstream outFile("lock.json");
	outFile << "{" << endl << "\"traceEvents\": [" << endl;

	map<int, vector<Entry> > perThreadEntries;
	while (getline(inFile, line)){
		istringstream iss(line);
		iss >> ds >> ds >> ds >> ds >> ds >> ds >> ds;
		getline(iss, action, ':');
		iss >> tid >> dc >> ts >> dc;
		if(action == " LOCK_ACQUIRE"){
			iss >> duration >> dc;
			getline(iss, enter, ',');
			getline(iss, exit, ',');
			getline(iss, obj, ',');
			getline(iss, location);
			if(!comma){
				comma = true;
			} else {
				outFile << "," << endl;
			}
			// outFile << fixed << "{ \"pid\":" << pid << ", \"tid\":" << tid << ", \"ts\":" << ts/1000.0
			// << ",\"dur\":\"" << duration/1000.0  << "\", \"ph\":\"X\", \"name\":\""  << hex << obj << dec
			// << "\", \"args\":{ \"type\":\"acquire\", \"enter\":\"" << enter << "\", \"exit\":\"" << exit << "\" } }";
			outFile << fixed << "{ \"pid\":" << pid << ", \"tid\":" << tid << ", \"ts\":" << ts/1000.0
			<< ",\"dur\":\"" << duration/1000.0  << "\", \"ph\":\"X\", \"name\":\"contention\","
			<< " \"args\":{ \"obj\":\"" << hex << obj << dec << "\", \"enter\":\"" << enter << "\", \"exit\":\"" << exit
			<< "\", \"location\":\"" << location << "\" } }";
			// outFile << fixed << "{ \"pid\":" << pid << ", \"tid\":" << tid << ", \"ts\":" << ts/1000.0
			// << ", \"ph\":\"B\", \"name\":\""  << hex << obj << dec
			// << "\", \"args\":{ \"type\":\"acquire\", \"enter\":\"" << enter << "\", \"exit\":\"" << exit << "\" } }," << endl;
			// outFile << fixed << "{ \"pid\":" << pid << ", \"tid\":" << tid << ", \"ts\":" << (ts + duration)/1000.0
		 //  << ", \"ph\":\"E\", \"name\":\""  << hex << obj << dec << "\" }";
		} else if(action == " LOCK_INFLATE"){
			getline(iss, type, ',');
			getline(iss, obj, ',');
			if(!comma){
				comma = true;
			} else {
				outFile << "," << endl;
			}
			outFile << fixed << "{ \"pid\":" << pid << ", \"tid\":" << tid << ", \"ts\":" << ts/1000.0
			<< ", \"ph\":\"i\", \"s\":\"t\", \"name\":\""  << hex << obj << dec << "\", \"args\":{ \"type\":\"" << type << "\" } }";
		} else if(action == " LOCK_WAIT"){
			iss >> duration >> dc;
			getline(iss, obj, ',');
			getline(iss, location);
			if(!comma){
				comma = true;
			} else {
				outFile << "," << endl;
			}
			// outFile << fixed << "{ \"pid\":" << pid << ", \"tid\":" << tid << ", \"ts\":" << ts/1000.0
			// << ",\"dur\":\"" << duration/1000.0  << "\", \"ph\":\"X\", \"name\":\""  << hex << obj << dec
			// << "\", \"args\":{ \"type\":\"wait\"} }";
			outFile << fixed << "{ \"pid\":" << pid << ", \"tid\":" << tid << ", \"ts\":" << ts/1000.0
			<< ",\"dur\":\"" << duration/1000.0  << "\", \"ph\":\"X\", \"name\":\"wait\","
			<< " \"args\":{ \"obj\":\"" << hex << obj << dec
			<< "\", \"location\":\"" << location << "\" } }";
		} else {
			assertf(false, "wrong action %s", action.c_str());
		}

		// cout << line << endl;

		// switch(s){
		// 	case IDLE:
		// 		if(action == "LOCK_ACQUIRE"){
		// 			string location;
		// 			iss >> dc;
		// 			getline(iss, location);
		// 			outFile << fixed << "{ \"pid\":" << pid << " , \"tid\":" << tid << " , \"ts\":" << ts/1000.0 << ", \"ph\":\"B\", \"name\":\""  << hex << obj << dec << "\", \"args\":{ \"type\":\"" << type << "\", \"location\":\"" << location << "\" } }";
		// 			if(type == "THIN")	s = WAIT_THIN;
		// 			else if (type == "FAT") s = WAIT_FAT;
		// 			else 	assertf(false, "wrong type");
		// 		} else if(action == "LOCK_INFLATE"){
		// 			assertf(type == "HASH" || type == "RECURSION" || type == "WAIT", "cannot inflate in IDLE, obj %s", obj.c_str());
		// 			outFile << fixed << "{ \"pid\":" << pid << " , \"tid\":" << tid << " , \"ts\":" << ts/1000.0 << ", \"ph\":\"i\", \"name\":\""  << hex << obj << dec << "\", \"args\":{ \"type\":\"" << type << "\" } }";
		// 		} else if(action == "LOCK_WAIT"){
		// 			assertf(type == "START", "can only start Wait() in IDLE, obj %s", obj.c_str());
		// 			outFile << fixed << "{ \"pid\":" << pid << " , \"tid\":" << tid << " , \"ts\":" << ts/1000.0 << ", \"ph\":\"B\", \"name\":\""  << hex << obj << dec << "\", \"args\":{ \"type\":\"WAIT\" } }";
		// 			s = WAIT;
		// 		} else {
		// 			assertf(false, "wrong ACTION %s obj %s", action.c_str(), obj.c_str());
		// 		}
		// 		break;
		// 	case WAIT_THIN:
		// 		if(action == "LOCK_ACQUIRE"){
		// 			if(type == "FAT"){
		// 				// someone else inflate the lock
		// 				s = WAIT_FAT;
		// 			} else if(type == "THIN"){
		// 				// someone else get the lock when we try to inflate
		// 				s = WAIT_THIN;
		// 			} else {
		// 				assertf(false, "wrong type %s", type.c_str());
		// 			}
		// 			string location;
		// 			iss >> dc;
		// 			getline(iss, location);
		// 			outFile << fixed << "{ \"pid\":" << pid << " , \"tid\":" << tid << " , \"ts\":" << ts/1000.0 << ", \"ph\":\"E\" }," << endl;
		// 			outFile << fixed << "{ \"pid\":" << pid << " , \"tid\":" << tid << " , \"ts\":" << ts/1000.0 << ", \"ph\":\"B\", \"name\":\""  << hex << obj << dec << "\", \"args\":{ \"type\":\"" << type << "\", \"location\":\"" << location << "\" } }";
		// 		} else if(action == "LOCK_GET"){
		// 			assertf(type == "THIN", "can only get THIN when wait for THIN");
		// 			s = IDLE;
		// 			outFile << fixed << "{ \"pid\":" << pid << " , \"tid\":" << tid << " , \"ts\":" << ts/1000.0 << ", \"ph\":\"E\" }";
		// 		} else if(action == "LOCK_INFLATE"){
		// 			assertf(type == "CONTENTION" || type == "WAIT" || type == "HASH", "inflate lock when WAIT_THIN must be CONTENTION, obj %s", obj.c_str());
		// 			outFile << fixed << "{ \"pid\":" << pid << " , \"tid\":" << tid << " , \"ts\":" << ts/1000.0 << ", \"ph\":\"i\", \"name\":\"" << hex << obj << dec << "\", \"args\":{ \"type\":\"" << type << "\" } }";
		// 		} else {
		// 			assertf(false, "wrong ACTION %s, obj %s", action.c_str(), obj.c_str());
		// 		}
		// 		break;
		// 	case WAIT_FAT:
		// 		if(action == "LOCK_GET"){
		// 			assertf(type == "FAT", "can only get fat in WAIT_FAT");
		// 			outFile << fixed << "{ \"pid\":" << pid << " , \"tid\":" << tid << " , \"ts\":" << ts/1000.0 << ", \"ph\":\"E\" }";
		// 			s = IDLE;
		// 		} else {
		// 			assertf(false, "wrong ACTION %s, obj %s", action.c_str(), obj.c_str());
		// 		}
		// 		break;
		// 	case WAIT:
		// 		assertf(action == "LOCK_WAIT" && type == "END", "must first be waken up, obj %s", obj.c_str());
		// 		outFile << fixed << "{ \"pid\":" << pid << " , \"tid\":" << tid << " , \"ts\":" << ts/1000.0 << ", \"ph\":\"E\" }";
		// 		s = IDLE;
		// 		break;
		// 	default:
		// 		assertf(false, "wrong state %d", s);
		// }
	}
	inFile.close();

	ifstream inFile2(pids + ".name");
	while (getline(inFile2, line)){
		istringstream iss(line);
		iss >> ds >> ds >> ds >> ds >> ds >> ds >> dc >> ds >> tid >> dc;
		string name;
		getline(iss, name);
		outFile << "," << endl;
		outFile << "{ \"name\":\"thread_name\", \"pid\":" << pid << ", \"tid\":" << tid << ", \"ph\":\"M\", \"args\":{ \"name\":\"" << tid << "-" << name << "\" } }";
	}
	outFile << "," << endl;
	outFile << "{ \"name\":\"thread_name\", \"pid\":" << pid << ", \"tid\":" << pid << ", \"ph\":\"M\", \"args\":{ \"name\":\"" << pid << "-main\" } }";


	inFile2.close();

	outFile << endl << "]," << endl << "\"displayTimeUnit\":\"ns\"," << endl << "\"meta_user\": \"uber\"," << endl <<  "\"meta_cpu\": \"2\"" << endl << "}";

	outFile.close();
	return 0;
}

