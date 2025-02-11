// file_list.h
#ifndef __UI_FILE_LIST_HPP__
#define __UI_FILE_LIST_HPP__

#include <lvgl.h>
#include <Arduino.h>

#include <vector>
#include <functional>

#include "machine/machine_interface.hpp"

class FileList {
public:
    using file_click_cb_t = std::function<void (std::string& filename)>; // Callback for file click
 
    FileList(lv_obj_t* parent, const char* path, MachineInterface* machine, file_click_cb_t onFileClick);
    ~FileList() {} // Default destructor - LVGL handles object deletion

    void initRefreshFloatBtn();
    void refresh();
    void showFiles(const std::vector<std::string>& files);

    lv_obj_t *list;
    lv_obj_t* floatBtn = nullptr;
private:
    MachineInterface* machine; // Assuming interface has a 'machine' member
    std::string path;
    file_click_cb_t onFileClick;

    // Private helper methods
    std::string pathCapitalize();
    void connected(MachineInterface* machine);
    void refresh_(); // Note the trailing underscore
    void initEmpty();
    void updatedFiles(MachineInterface* machine, const char* path, const std::vector<std::string>& files);
    
    void btnClick(lv_event_t* e);
    void refreshCb(lv_event_t* e);
    void connectedCb(MachineInterface *machine);
    void updatedFilesCb(MachineInterface* machine, std::string, const std::vector<std::string>& files);
};

#endif // __UI_FILE_LIST_HPP__
