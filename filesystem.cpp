#include <memory>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <assert.h>
#include <algorithm>

constexpr std::size_t disk_size = 1 << 25;
std::vector<char> disk(disk_size);

typedef unsigned int pointer; // x32
//typedef unsigned long long pointer; // x64
constexpr unsigned short blockSize = sizeof(pointer);
constexpr std::size_t pointer_max = disk_size;
bool initializing = true;

/*
Algorithm
divide into blocks of 32

Folder Structure: 32 bytes
>pointer to next element, >size of name, 16 bytes of data, >pointer to data,  >pointer to first element in folder

File Structure: 32 bytes
>pointer to next element, >size of name, 16 bytes of data, >pointer to data, >size of data,

Data Structure: 32 bytes
>pointer to next element, >28 bytes of data

First 32 bytes: pointer to data
Next 32 bytes: head directory
*/

pointer getPointer(const pointer& location){
    pointer res = 0;
    for(pointer i = 0; i < blockSize; i++){
        res += (pointer)(disk[location + i]+128) << (i<<3);
    }
    return res;
}

void writePointer(const pointer& location, pointer value){
    for(pointer i = location; i < location + blockSize; i++){
        disk[i] = (value % 256)-128;
        value /= 256;
    }
}

pointer requestMemory(){
    pointer memoryLocation = getPointer(0);
    if(memoryLocation == 0)
        throw std::runtime_error("out of memory!");
    writePointer(0, getPointer(memoryLocation));
    writePointer(memoryLocation, 0); // safety
    return memoryLocation;
}

void returnMemory(pointer location){
    pointer nextBlock = getPointer(0);
    writePointer(0, location);
    writePointer(location, nextBlock);
}

std::string getString(int start, int end){ // not inclusive
    return std::string(disk.begin() + start, disk.begin() + end);
}

std::string dataToString(const std::vector<char>& data){
    return std::string(data.begin(), data.end());
}

void init(){
    for(int i = 0; i < disk_size; i+=blockSize*8){
        if(i+blockSize*8 == disk_size)
            writePointer(i, 0);
        else
            writePointer(i, i+blockSize*8);
    }
    writePointer(0, blockSize*16); //point to next available data space
    //root directory
    writePointer(blockSize*8, 0); // no other file same level
    writePointer(blockSize*9, 1);  // name has size 1
    disk[blockSize*10] = '\\';  // writing name
    writePointer(blockSize*14, 0); // no need for any extra data, ptr to extra data is 0
    writePointer(blockSize*15, 0); // no files in folder yet
    initializing = false;
}

void getData(pointer location, std::vector<char>& data, pointer amt){ // data block size 32 bytes uwu
    for(pointer i = location + blockSize; i < location + blockSize*8; i++){
        if(!amt) continue;
        data.push_back(disk[i]);
        amt--;
    }
    location = getPointer(location);
    if(location)
        getData(location, data, amt);
}

void writeData(pointer location, const std::vector<char>&data, pointer start){
    for(pointer i = location + blockSize; i < location + blockSize*8; i++){
        if(start >= data.size()) continue;
        disk[i] = data[start++];
    }
    if(start >= data.size())
        writePointer(location, 0); // no need for any more data, point to nullptr
    else{
        writePointer(location, requestMemory());
        location = getPointer(location);
        writeData(location, data, start);
    }
}

void deleteData(pointer location){ //0 -> [data] -> [next data]
    if (location == 0) return;
    pointer nextData = getPointer(0);
    writePointer(0, location);
    while(getPointer(location)){
        location = getPointer(location);
    }
    writePointer(location, nextData);
}

std::string getName(pointer location){
    int nameSize = getPointer(location + blockSize);
    if(nameSize <= blockSize*4) return getString(location + blockSize*2, location + blockSize*2 + nameSize);
    std::vector<char> data;
    getData(getPointer(location + blockSize*6), data, nameSize - blockSize*4);
    return getString(location + blockSize*2, location + blockSize*6) + dataToString(data);
}

pointer findPathLocation(pointer location, std::string filename){ //return UINT_MAX if fail
    //find name end
    int nameEnd = 0;
    while(nameEnd < filename.size() && filename[nameEnd] != '\\'){
        nameEnd++;
    }
    if(nameEnd < filename.size())
        nameEnd++;
    std::string targetName = filename.substr(0,nameEnd);
    if(targetName.back() != '\\' && nameEnd < filename.size()) return pointer_max; // not a folder, but has stuff???

    std::string name = "";
    while(name != targetName && location != 0){
        name = getName(location);
        if(name != targetName)
        location = getPointer(location);
    }
    if(location == 0)
        return pointer_max; // can't find name

    if(nameEnd == filename.size()) return location;
    //set filename to new filename
    filename = filename.substr(nameEnd, filename.size() - nameEnd);
    //set location to new location
    location = getPointer(location + blockSize*7);
    return findPathLocation(location, filename);
}

std::pair<pointer, bool> findPrevPathLocation(pointer prevlocation, pointer location, std::string filename){ //return UINT_MAX if fail, mostly for deletion uwus
    //find name end
    int nameEnd = 0;
    while(nameEnd < filename.size() && filename[nameEnd] != '\\'){
        nameEnd++;
    }
    if(nameEnd < filename.size())
        nameEnd++;
    std::string targetName = filename.substr(0,nameEnd);
    if(targetName.back() != '\\' && nameEnd < filename.size()) return {pointer_max, false}; // not a folder, but has stuff???

    std::string name = "";
    bool locationUpdated = false;
    while(name != targetName && location != 0){
        name = getName(location);
        if(name != targetName){
            prevlocation = location;
            location = getPointer(location);
            locationUpdated = true;
        }
    }
    if(location == 0) return {pointer_max, false}; // can't find name
    
    if(nameEnd == filename.size()) return {prevlocation, locationUpdated};
    //set filename to new filename
    filename = filename.substr(nameEnd, filename.size() - nameEnd);
    //set location to new location
    prevlocation = location;
    location = getPointer(location + blockSize*7);

    return findPrevPathLocation(prevlocation, location, filename);
}

std::vector<char> ReadFile(const std::string& filename){
    pointer location = findPathLocation(blockSize*8, filename); //32 is root directory
    if(location == pointer_max){
        throw std::runtime_error(filename+" is not a valid filename");
    }
    int nameSize = getPointer(location + blockSize);
    int dataSize = getPointer(location + blockSize*7);
    std::vector<char> data = {disk.begin() + location + blockSize*2, disk.begin() + location + blockSize*2 + std::min(dataSize, blockSize*4)};
    if(dataSize > blockSize*4){
        getData(getPointer(location + blockSize*6), data, dataSize - blockSize*4);
    }
    data.erase(data.begin(), data.begin() + nameSize); // this is the name
    return data;
}

bool DeleteFileNoExcept(const std::string&filename) noexcept{
    auto location = findPrevPathLocation(pointer_max, blockSize*8, filename);
    if(location.first == pointer_max){
        return true; //failed
    }
    if(location.second){
        pointer location2 = getPointer(location.first);
        assert(findPathLocation(blockSize*8, filename) == location2); // debugging purposes
        deleteData(getPointer(location2 + blockSize*6)); // delete the data
        writePointer(location.first, getPointer(location2));
        returnMemory(location2);
    }
    else{
        pointer location2 = getPointer(location.first + blockSize * 7);
        assert(findPathLocation(blockSize*8, filename) == location2); // debugging purposes
        deleteData(getPointer(location2 + blockSize*6)); // delete the data
        writePointer(location.first + blockSize * 7, getPointer(location2));
        returnMemory(location2);
    }
    return false; // succeeded!
}

void WriteFile(const std::string &filename, std::vector<char> data){
    int rightMostSlash = filename.size() - 1;
    while(rightMostSlash >= 0 && filename[rightMostSlash] != '\\'){
        rightMostSlash--;
    }
    if(rightMostSlash < 0){
        throw std::runtime_error(filename+" is not a valid filename");
    }
    rightMostSlash++;
    std::string foldername = filename.substr(0,rightMostSlash);
    std::string filename2 = filename.substr(rightMostSlash);
    if(filename2.size() == 0){
        throw std::runtime_error(filename+" is not a valid filename");
    }
    pointer folderLocation = findPathLocation(blockSize*8, foldername);
    if(folderLocation == pointer_max){
        throw std::runtime_error(foldername+" is not a valid foldername");
    }
    DeleteFileNoExcept(filename);
    data.insert(data.begin(), filename2.begin(), filename2.end());
    //create file @ folderLocation
    pointer nextFileLocation = getPointer(folderLocation + blockSize*7);
    pointer fileLocation = requestMemory();
    writePointer(folderLocation+blockSize*7, fileLocation);
    writePointer(fileLocation, nextFileLocation);
    writePointer(fileLocation + blockSize, filename2.size());
    writePointer(fileLocation + blockSize * 7, data.size());

    //thing
    pointer idx = 0;
    for(pointer i = fileLocation + blockSize * 2; i < fileLocation + blockSize * 6; i++){
        if(idx < data.size())
            disk[i] = data[idx++];
    }
    if(idx < data.size()){ // need extra memory to store data
        pointer location = requestMemory();
        writePointer(fileLocation + blockSize*6, location);
        writeData(location, data, idx);
    }
    else{
        writePointer(fileLocation + blockSize*6, 0);
    }
}

void DeleteFile(const std::string& filename){
    if(DeleteFileNoExcept(filename)){
        throw std::runtime_error(filename + " is not a valid filename");
    }
}

std::size_t FileSize(const std::string& filename){
    return 0;
}

void CreateFolder(const std::string& filename){    
    //>pointer to next element, >size of name, 16 bytes of data, >pointer to data,  >pointer to first element in folder
    if(filename.size() == 0 || filename.back() != '\\'){
        throw std::runtime_error(filename+" is not a valid foldername");
    }
    int rightMostSlash = filename.size() - 2;
    while(rightMostSlash >= 0 && filename[rightMostSlash] != '\\'){
        rightMostSlash--;
    }
    if(rightMostSlash < 0){
        throw std::runtime_error(filename+" is not a valid foldername");
    }
    rightMostSlash++;
    std::string foldername = filename.substr(0,rightMostSlash);
    std::string filename2 = filename.substr(rightMostSlash);
    pointer folderLocation = findPathLocation(blockSize*8, foldername);
    if(folderLocation == pointer_max){
        throw std::runtime_error(foldername+" is not a valid foldername");
    }
    DeleteFileNoExcept(filename);
    std::vector<char> data = {filename2.begin(), filename2.end()};
    //create folder @ folderLocation
    pointer nextFileLocation = getPointer(folderLocation + blockSize*7);
    pointer fileLocation = requestMemory();
    writePointer(folderLocation+blockSize*7, fileLocation);
    writePointer(fileLocation, nextFileLocation);
    writePointer(fileLocation + blockSize, data.size());
    writePointer(fileLocation + blockSize * 7, 0);

    pointer idx = 0;
    for(pointer i = fileLocation + blockSize * 2; i < fileLocation + blockSize * 6; i++){
        if(idx < data.size())
            disk[i] = data[idx++];
    }
    if(idx < data.size()){ // need extra memory to store data
        pointer location = requestMemory();
        writePointer(fileLocation + blockSize*6, location);
        writeData(location, data, idx);
    }
    else{
        writePointer(fileLocation + blockSize*6, 0);
    }
}

void DeleteFolder(const std::string& filename){
    if(DeleteFileNoExcept(filename)){
        throw std::runtime_error(filename + " is not a valid foldername");
    }
}

std::vector<std::string> ListFiles(const std::string& filename){
    if(filename.size() == 0 || filename.back() != '\\'){
        throw std::runtime_error(filename + " is not a valid foldername");
    }
    pointer location = findPathLocation(blockSize*8, filename);
    if(location == pointer_max){
        throw std::runtime_error(filename + " is not a valid foldername");
    }
    location = getPointer(location + blockSize * 7);
    std::vector<std::string> files;
    while(location != 0){
        files.push_back(getName(location));
        location = getPointer(location);
    }
    std::sort(files.begin(), files.end(), [](const std::string&a, const std::string&b){
        if(a == b) return false;
        if(a.size() == 0) return true;
        if(b.size() == 0) return false;
        if(a.back() == '\\' && b.back() != '\\') return true;
        if(a.back() != '\\' && b.back() == '\\') return false;
        return a < b;
    });
    return files;
}

std::vector<std::string> split (const std::string &s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss (s);
    std::string item;
    while (ss>>item) {
        result.push_back (item);
    }
    return result;
}

int main(){
	init();
    std::string cd = "\\";
    std::cout<<"File System!!! Type help to start"<<std::endl<<std::endl;
    while(true){
        std::vector<std::string> command;
        std::string s;
        std::getline(std::cin, s);
        std::cout<<std::endl;
        command = split(s, ' ');
        if(command.size() == 0)
            continue;
        try{
            if(command[0] == "help"){
                std::cout<<R"(
Commands:
cd Current Directory
cd [directory] Set directory
create [folder] Create folder
delete [folder] Delete folder
write [file] [data] Write to file
delete [file] Delete file
list Lists files in current directory
            )"<<std::endl;
            }
            else if(command[0] == "cd"){
                std::string prevcd = cd;
                if(command.size() >= 2){
                    if(command[1].back() != '\\'){
                        command[1] += "\\";
                    }
                    if(command[1][0] == '\\'){
                        cd = command[1];
                    }
                    else{
                        cd += command[1];
                    }
                }
                if(findPathLocation(blockSize*8, cd) == pointer_max){
                    std::cout<<"Invalid Directory"<<std::endl;
                    cd = prevcd;
                }
                std::cout<<"Current Directory: "<<cd<<std::endl;
            }
            else if(command[0] == "list"){
                std::cout<<"Current Directory: "<<cd<<std::endl;
                auto files = ListFiles(cd);
                if(files.size()){
                    for(auto &file : files){
                        std::cout<<file<<std::endl;
                    }
                }
                else{
                    std::cout<<"Empty Directory"<<std::endl;
                }
            }
            else if(command[0] == "create"){
                if(command.size() >= 2){
                    if(command[1].back() != '\\'){
                        command[1] += '\\';
                    }
                    if(command[1][0] == '\\'){
                        CreateFolder(command[1]);
                        std::cout<<"Created "<<command[1]<<std::endl;
                    }
                    else{
                        CreateFolder(cd + command[1]);
                        std::cout<<"Created "<<cd<<command[1]<<std::endl;
                    }
                }
                else{
                    std::cout<<"You need to give me something to create, idiot"<<std::endl;
                }
            }
            else if(command[0] == "delete"){
                if(command.size() >= 2){
                    if(command[1][0] == '\\'){
                        if(command[1].back() == '\\'){
                            DeleteFolder(command[1]);
                        }
                        else{
                            DeleteFile(command[1]);
                        }
                        std::cout<<"Deleted "<<command[1]<<std::endl;
                    }
                    else{
                        if(command[1].back() == '\\'){
                            DeleteFolder(cd+command[1]);
                        }
                        else{
                            DeleteFile(cd+command[1]);
                        }
                        std::cout<<"Deleted "<<cd<<command[1]<<std::endl;
                    }
                }
                else{
                    std::cout<<"You need to give me something to delete, idiot"<<std::endl;
                }
            }
            else if(command[0] == "write") {
                if(command.size() >= 3){
                    if(command[1][0] == '\\'){
                        std::vector<char> data = {command[2].begin(), command[2].end()};
                        WriteFile(command[1], data);
                        std::cout<<"Wrote to "<<command[1]<<" with "<<command[2]<<std::endl;
                    }
                    else{
                        std::vector<char> data = {command[2].begin(), command[2].end()};
                        WriteFile(cd+command[1], data);
                        std::cout<<"Wrote to "<<cd<<command[1]<<" with "<<command[2]<<std::endl;
                    }
                }
                else{
                    std::cout<<"You need give me something to write, idiot"<<std::endl;
                }
            }
            else if(command[0] == "read"){
                if(command.size() >= 2){
                    std::vector<char> data;
                    if(command[1][0] == '\\'){
                        data = ReadFile(command[1]);
                        std::cout<<"Reading file "<<command[1]<<std::endl;
                    }
                    else{
                        data = ReadFile(cd+command[1]);
                        std::cout<<"Reading file "<<cd<<command[1] <<std::endl;
                    }
                    std::string data2 = {data.begin(), data.end()};
                    std::cout<<data2<<std::endl;
                }
                else{
                    std::cout<<"You need give me something to read, idiot"<<std::endl;
                }
            }
            else{
                std::cout<<"Invalid command - Type \"help\" for command list"<<std::endl;
            }
        }
        catch(const std::exception&e){
            std::cout<<e.what()<<std::endl;
        }
        std::cout<<std::endl;
    }
}
