#pragma once
#include <Arduino.h>

enum class MenuAction {
    None,
    Calibration,
    ViewCalibration,
    Close,
    // Workout actions
    Start,
    Pause,
    Resume,
    Stop,
    Lap
};

struct MenuItem {
    const char* label;
    MenuAction action;
};

class Menu {
public:
    static const int MAX_ITEMS = 8;

    Menu();

    void addItem(const char* label, MenuAction action);
    void open();
    void close();
    bool isOpen() const { return _isOpen; }

    // Navigation
    void selectNext();
    void selectPrev();
    MenuAction confirm();  // Returns action of selected item

    // Direct selection (for touch)
    MenuAction selectByIndex(int index);

    // Get last confirmed action (and clear it)
    MenuAction getLastAction();

    // Getters for rendering
    int getItemCount() const { return _itemCount; }
    int getSelectedIndex() const { return _selectedIndex; }
    const MenuItem* getItem(int index) const;
    const char* getTitle() const { return _title; }
    void setTitle(const char* title) { _title = title; }

private:
    MenuItem _items[MAX_ITEMS];
    int _itemCount = 0;
    int _selectedIndex = 0;
    bool _isOpen = false;
    const char* _title = "MENU";
    MenuAction _lastAction = MenuAction::None;
};
