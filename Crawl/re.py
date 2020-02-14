# -*- coding: utf-8 -*-
import os
import requests
from lxml import html
import urllib
import urllib2

from selenium import webdriver
from selenium.webdriver.support.ui import Select

headers = {
    'Host': 'readingeggs.com.au',
    'Accept-Language': 'en-US,en;q=0.9,zh-CN;q=0.8,zh;q=0.7',
    'Accept-Encoding': 'gzip, deflate, br',
    'Connection': 'keep-alive',
    'Pragma': 'no-cache',
    'Cache-Control': 'no-cache',
    'Upgrade-Insecure-Requests': '1',
    'Accept': 'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8',
    'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)'
                  'AppleWebKit/537.36 (KHTML, like Gecko)'
                  'Chrome/74.0.3729.157 Safari/537.36',
}

def save(text, filename='temp', path='download'):
    fpath = os.path.join(path, filename)
    with open(fpath, 'wb') as  f:
        print('output:', fpath)
        f.write(text)
        f.close()


def save_file(file_url):
    print(file_url)
    resp = requests.get(file_url)
    page = resp.content
    filename = file_url.split('/')[-1]
    print(filename)
    save(page, filename)


def crawl(url):
    driver = webdriver.Firefox()
    driver.get("https://app.readingeggs.com/login")
    driver.find_element_by_xpath('//*[@id="username"]').send_keys('username')
    driver.find_element_by_xpath('//*[@id="password"]').send_keys('password')
    driver.find_element_by_xpath('//*[@id="login-page"]/div[1]/div[1]/form/fieldset/input[5]').click()

    driver.find_element_by_xpath('//*[@id="accordion-navbar"]/ul[1]/li[2]/a').click()
    driver.find_element_by_xpath('//*[@id="accordion-navbar"]/ul[1]/li[2]/ul/li[1]/a').click()

    # print(driver.page_source)
    root = html.fromstring(driver.page_source)
    #file_urls = root.xpath('//a[contains(@href, "/lesson_pdfs/student/readingeggs/")]')
    #file_urls = root.xpath('//a[contains(@href, "/rex_comprehension/parent_worksheets/")]')
    #file_urls = root.xpath('//a[contains(@href, "/rex_spelling/parent_worksheets/")]')
    file_urls = root.xpath('//a[contains(@href, "/lesson_pdfs/student/mathseeds/")]')

    for file_url in file_urls:
        #print (file_url.attrib['href'])
        save_file(file_url.attrib['href'])



if __name__ == '__main__':
    crawl("https://app.readingeggs.com/login")
