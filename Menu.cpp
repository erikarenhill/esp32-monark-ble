#include "Menu.h"

Menu::Menu() {
    _itemCount = 0;
    _selectedIndex = 0;
    _isOpen = false;
}

void Menu::addItem(const char* label, MenuAction action) {
    if (_itemCount < MAX_ITEMS) {
        _items[_itemCount].label = label;
        _items[_itemCount].action = action;
        _itemCount++;
    }
}

void Menu::open() {
    _isOpen = true;
    _selectedIndex = 0;
}

void Menu::close() {
    _isOpen = false;
}

void Menu::selectNext() {
    if (_itemCount > 0) {
        _selectedIndex = (_selectedIndex + 1) % _itemCount;
    }
}

void Menu::selectPrev() {
    if (_itemCount > 0) {
        _selectedIndex = (_selectedIndex - 1 + _itemCount) % _itemCount;
    }
}

MenuAction Menu::confirm() {
    if (_itemCount > 0 && _selectedIndex < _itemCount) {
        _lastAction = _items[_selectedIndex].action;
        close();
        return _lastAction;
    }
    return MenuAction::None;
}

MenuAction Menu::selectByIndex(int index) {
    if (index >= 0 && index < _itemCount) {
        _selectedIndex = index;
        return confirm();
    }
    return MenuAction::None;
}

MenuAction Menu::getLastAction() {
    MenuAction action = _lastAction;
    _lastAction = MenuAction::None;
    return action;
}

const MenuItem* Menu::getItem(int index) const {
    if (index >= 0 && index < _itemCount) {
        return &_items[index];
    }
    return nullptr;
}
