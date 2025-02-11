// FileList.cpp
#include "file_list.hpp"

#include "ui/event_handler.hpp"

FileList::FileList(lv_obj_t* parent, const char* path, MachineInterface* machine, file_click_cb_t onFileClick)
    : machine(machine), path(path), onFileClick(onFileClick) {
    list = lv_list_create(parent);
 
    machine->addConnectedCb(std::bind(&FileList::connectedCb, this, std::placeholders::_1));
    machine->addFilesChangedCb(std::bind(&FileList::updatedFilesCb, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), path);

    initEmpty();
}


void FileList::initRefreshFloatBtn() {
    // Not implemented yet (requires tab object which isn't defined.)
    // Need tab as a member of the class.
}


std::string FileList::pathCapitalize() {
    std::string s = this->path;
    if (!s.empty()) {
        s[0] = toupper(s[0]);
    }
    return s;
}

void FileList::connectedCb(MachineInterface* machine) {
    
}

void FileList::connected(MachineInterface* machine) {
    if (machine->isConnected()) {
        refresh();
    }
}

void FileList::refresh() {
    machine->listFiles(path);
}

void FileList::btnClick(lv_event_t* e) {
    if (this->onFileClick != nullptr) {
        lv_obj_t* target = evt_target_obj(e);
        std::string filename = lv_list_get_button_text(list, target);
        this->onFileClick(filename);
    }
}

void FileList::refreshCb(lv_event_t* e) {
    refresh();
}

void FileList::initEmpty() {
    lv_obj_clean(list);

    lv_obj_t* lbl = lv_list_add_text(list, pathCapitalize().c_str());
    lv_obj_set_height(lbl, 20);

    lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_REFRESH, "Waiting for connection...");
    //lv_obj_add_event_cb(list, FileList::refreshCb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_fn(list, LV_EVENT_CLICKED, evt_bind(FileList::refreshCb, this));
}

void FileList::showFiles(const std::vector<std::string>& files) {
    lv_obj_clean(list);

    lv_obj_t* lbl = lv_list_add_text(list, pathCapitalize().c_str());
    lv_obj_set_height(lbl, 20);

    std::vector<std::string> sortedFiles = files; // Copy to avoid modifying original
    std::sort(sortedFiles.begin(), sortedFiles.end());

    for (const auto& file : sortedFiles) {
        lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_FILE, file.c_str());
        lv_obj_add_event_fn(btn, LV_EVENT_CLICKED, evt_bind(FileList::btnClick, this));
        //lv_obj_add_event_cb(btn, FileList::btnClickCb, LV_EVENT_CLICKED, this);
    }
}

void FileList::updatedFilesCb(MachineInterface* machine, std::string, const std::vector<std::string>& files) {
    Serial.println("UPDATED");
    showFiles(files);
}