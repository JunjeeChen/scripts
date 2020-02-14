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
    driver.get("https://app.readingeggs.com/login")
    driver.find_element_by_xpath('//*[@id="username"]').send_keys('bcjm0322@hotmail.com')
    driver.find_element_by_xpath('//*[@id="password"]').send_keys('1qazZAQ!')
    driver.find_element_by_xpath('//*[@id="login-page"]/div[1]/div[1]/form/fieldset/input[6]').click()

    driver.find_element_by_xpath('//*[@id="accordion-navbar"]/ul[1]/li[2]/a').click()
    driver.find_element_by_xpath('//*[@id="accordion-navbar"]/ul[1]/li[2]/ul/li[1]/a').click()

    root = html.fromstring(driver.page_source)

    file_xpath = {'overview':'//a[contains(@href, "/lesson-overview/au/")]',
                  'placement_tests':'//a[contains(@href, "/placement-tests/")]',
                  'reading_eggs':'//a[contains(@href, "/lesson_pdfs/student/readingeggs/")]',
                  'reading_eggspress_comprehension':'//a[contains(@href, "/rex_comprehension/parent_worksheets/")]',
                  'reading_eggspress_spelling':'//a[contains(@href, "/rex_spelling/parent_worksheets/")]',
                  'mathseeds':'//a[contains(@href, "/lesson_pdfs/student/mathseeds/")]'}

    for key in file_xpath.keys():
        file_urls = root.xpath(file_xpath[key])
        for file_url in file_urls:
            save_file(key, file_url.attrib['href'])

if __name__ == '__main__':
    crawl("https://app.readingeggs.com/login")
