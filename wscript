#!/bin/python

from waflib import *
import os, sys

top = '.'
out = 'build'

projname = 'rainboa'

coreprog_name = projname

g_cflags = ["-Wall", "-Wextra", "-std=c++17"]
def btype_cflags(ctx):
	return {
		"DEBUG"   : g_cflags + ["-Og", "-ggdb3", "-march=core2", "-mtune=native"],
		"NATIVE"  : g_cflags + ["-Ofast", "-march=native", "-mtune=native"],
		"RELEASE" : g_cflags + ["-O3", "-march=core2", "-mtune=generic"],
	}.get(ctx.env.BUILD_TYPE, g_cflags)

def options(opt):
	opt.load("g++")
	opt.add_option('--build_type', dest='build_type', type="string", default='RELEASE', action='store', help="DEBUG, NATIVE, RELEASE")

def configure(ctx):
	ctx.load("g++")
	ctx.check(features='c cprogram', lib='pthread', uselib_store='PTHREAD')
	ctx.check(features='c cprogram', lib='dl', uselib_store='DL')
	ctx.check(features='c cprogram', lib='asterid', uselib_store='ASTERID')
	ctx.check(features='c cprogram', lib='locust', uselib_store='LOCUST')
	ctx.check(features='c cprogram', lib='pq', uselib_store='POSTGRES')
	ctx.check_cfg(path='pkg-config', args='--cflags --libs', package='botan-2', uselib_store='BOTAN')
	btup = ctx.options.build_type.upper()
	if btup in ["DEBUG", "NATIVE", "RELEASE"]:
		Logs.pprint("PINK", "Setting up environment for known build type: " + btup)
		ctx.env.BUILD_TYPE = btup
		ctx.env.CXXFLAGS = btype_cflags(ctx)
		Logs.pprint("PINK", "CXXFLAGS: " + ' '.join(ctx.env.CXXFLAGS))
		if btup == "DEBUG":
			ctx.define("RAINBOA_DEBUG", 1)
	else:
		Logs.error("UNKNOWN BUILD TYPE: " + btup)
		
def build(bld):
	bld_files = bld.path.ant_glob('src/*.cc')
	coreprog = bld (
		features = "cxx cxxprogram",
		target = coreprog_name,
		source = bld_files,
		uselib = ['PTHREAD', 'DL', 'ASTERID', 'LOCUST', 'POSTGRES', 'BOTAN'],
		includes = [os.path.join(top, 'src')],
	)
