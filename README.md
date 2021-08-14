# nanoanno

## What

This is a program that helps with quick small annotations (nano annotations if you will) in screenshots made with maim or something similar. Everything is in progress currently.

## Usage

You can open a file by passing it as the first argument: `nanoanno path/to/file.png` or by piping into the program. That's why `nanoanno file.png` and `cat file.png | nanoanno` behave the same. If there is both an argument and an image being piped into the program, piping will be prefered. 

You can pre-define the destination of a saved file with the `-o` flag. The save shortcuts will then just save the image to the destination saving you from the tiring file dialog (especially in day-to-day usage).

An example usage for screenshot taking may look like the following:

```sh
maim -s | nanoanno -o ~/$(date).png | xclip -selection clipboard -t image/png
```

First you select your screenshot through maim like you are used to. The result gets piped into nanoanno where you can do annoatations and where you are free to save the image to `~/$(date).png` or not. If you don't want to annotate your image then you can still decide on whether to save it or not (which may add one key press in total to that workflow in the "worst case"). The end result gets piped to your clipboard for further usage.

## Keybinds

|Keys|Action|
|---|---|
|holding left mouse button|painting|
|holding right mouse button|erasing|
|holding the middle mouse button, h, j, k, l|moving the image|
|mouse scrolling, u, i|scaling the image|
|x|undo all changes|
|s|save the image|
|w|quit without saving|
|q|quit and save the image|

## Video

![](https://cdn.discordapp.com/attachments/833686255446917123/874025047524798525/output.gif)

## TODO
- [x] make the second command line argument do its job
- [x] fix image dragging by holding the middle mouse button
- [ ] make the popup better looking
- [x] add clipboard support
- [x] interpolate between two drawn dots to create a more smooth line
- [x] eraser
- [x] text tool

my first C project - don't be harsh
