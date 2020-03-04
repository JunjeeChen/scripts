# -*- coding: utf-8 -*-
import os
import requests
from lxml import html
import urllib
import urllib2

from selenium import webdriver
from selenium.webdriver.support.ui import Select
import re



def save(text, filename='temp', subdir='', path='download'):
    dirName = os.path.join(path, subdir)
    if not os.path.exists(dirName):
        os.mkdir(dirName)

    fPath = os.path.join(path, filename)
    with open(fPath, 'wb') as  f:
        print('output:', fPath)
        f.write(text)
        f.close()

def save_file(dir, file_url):
    resp = requests.get(file_url)
    page = resp.content
    filename = file_url.split('/')[-1]
    save(page, dir + "\\" + filename, dir)

def crawl(url):
    driver = webdriver.Chrome()
    driver.get(url)

    root = html.fromstring(driver.page_source)

    file_urls = root.xpath('//a[contains(@href, "pdf")]')
    print(file_urls)
    for file_url in file_urls:
        print(dir(file_url))

    # < a
    # href = "https://acaraweb.blob.core.windows.net/acaraweb/docs/default-source/assessment-and-reporting-publications/naplan-2012-final-test---language-conventions-year-3.pdf?sfvrsn=2"
    # target = "_blank" > NAPLAN
    # 2012
    # final
    # test, language
    # conventions < / a >

    # for file_url in file_urls:
    #     print(file_url)
    #     # save_file(key, file_url.attrib['href'])

if __name__ == '__main__':
    crawl("https://www.acara.edu.au/assessment/naplan/past-naplan-papers/naplan-2008-2011-test-papers")
    # crawl("https://www.acara.edu.au/assessment/naplan/past-naplan-papers/naplan-2012-2016-test-papers")
