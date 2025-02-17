# web_scraper
Web scraper implemented in C++ with the Chromium Embedded Framework (CEF)

## How does it work?
This web scraper runs on top of the CEF, an open source framework designed to embed web-based UI's into various applications that is based off of the Chromium project.
It works by creating a new browser client (in this case a headless one aka no GUI) in which you programatically specify how it handled page loads, resource requests/responses, the content on those pages etc...
It has a huge API and can be accessed at the following link: https://cef-builds.spotifycdn.com/docs/132.3/index.html. There are various binary packages you can download and build to get started.
The version for this project is: 132.3.0+g8439bff+chromium-132.0.6834.57

## Why?
I got tired of dealing with various job search sites and decided to build my own.
It is built in C++ for flexibility of incorporating C/C++ processing frameworks and in the future Pytorch's C++ beta API.

## Is it faster being in C++?
I haven't done speed bench marking yet but regular HTML scrapers can't even handle javascript

## Is it better than enterprise API's out there?
It's certainly a lot cheaper because it costs $0 to run it locally

## Are you going to build it out more?
Yes, this is just the initial framework. There are plans to integrate various job aggregator public API's to bring in more data.

## Are you going to connect this to a data lake solution?
Yes, I am only one man and learning the CEF is difficult...but not impossible

## What future plans do you have?
- Configure for custom search parameters to be specified to the program via stdin
- Configure for custom max/min scrape parameters for number of links to be scraped
- 
- Connect job aggregator public API endpoints
- Implement data cleaning function to format the scraped HTML pages
- Implement various browsing signature techniques: cookie handling, proxy rotation
