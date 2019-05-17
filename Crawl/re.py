# -*- coding: utf-8 -*-
import os
import requests
from lxml import html

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


def save_image(image_url):
    print(image_url)
    resp = requests.get(image_url)
    page = resp.content
    filename = image_url.split('/')[-1]
    #print(filename)
    save(page, filename)


def crawl(url):
    resp = requests.get(url, headers=headers)
    page = resp.content
    root = html.fromstring(page)
    #image_urls = root.xpath('//img/@src')
    image_urls = root.xpath('//img[contains(@src, "/images/")]')
    #print (image_urls)

    for image_url in image_urls:
        #print (image_url.attrib['src'])
        #print(url+image_url.attrib['src'])
        save_image(url+image_url.attrib['src'])
        # break

if __name__ == '__main__':
    # 注意在运行之前，先确保该文件的同路径下存在一个download的文件夹, 用于存放爬虫下载的文件
    url = 'https://readingeggs.com.au'
    crawl(url)
