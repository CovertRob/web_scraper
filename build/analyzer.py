import re

def extract_https_strings(filename):
    # Open the file and read its content
    with open(filename, "r") as f:
        content = f.read()
    
    # Regular expression to capture strings starting with "https" until a whitespace or quote.
    pattern = "http[^\s\"']+"
    matches = re.findall(pattern, content)
    
    # Print out each matched URL
    for match in matches:
        print(match)

if __name__ == "__main__":
    extract_https_strings("/home/robert_feconda/cpp_dev/web_scraper/build/html_scrape.txt")
