# -*- coding: utf-8 -*-
import os
import requests
from lxml import html
import urllib
import urllib2

from selenium import webdriver
from selenium.webdriver.support.ui import Select

from bs4 import BeautifulSoup

def save(text, filename='temp', path='download'):
    fpath = os.path.join(path, filename)
    with open(fpath, 'wb') as  f:
        print('output:', fpath)
        f.write(text)
        f.close()


def save_file(file_url):
    #print(file_url)
    resp = requests.get(file_url)
    page = resp.content
    filename = file_url.split('/')[-1]
    # file_name = filename.split('?')[0]

    save(page, filename)


def getURL(page):
    start_link = page.find("a href")
    if start_link == -1:
        return None, 0

    start_quote = page.find('"', start_link)
    end_quote = page.find('"', start_quote+1)
    url = page[start_quote+1 : end_quote]
    return url, end_quote

def crawl():
    # (1-36)
    # www.k12reader.com/spelling/1st_grade_spelling_words_week_1.pdf

    for idx in range(36):
        url = "https://www.k12reader.com/spelling/1st_grade_spelling_words_week_"+str(idx+1)+".pdf"
        # url = "https://www.k12reader.com/spelling/2nd_grade_spelling_words_week_"+str(idx+1)+".pdf"
        # url = "https://www.k12reader.com/spelling/3rd_grade_spelling_words_week_" + str(idx + 1) + ".pdf"
        # url = "https://www.k12reader.com/spelling/4th_grade_spelling_words_week_" + str(idx + 1) + ".pdf"
        # url = "https://www.k12reader.com/spelling/5th_grade_spelling_words_week_" + str(idx + 1) + ".pdf"

        # print url
        save_file(url)

if __name__ == '__main__':
    # crawl("https://www.acara.edu.au/assessment/naplan/past-naplan-papers/naplan-2012-2016-test-papers")
    crawl()

