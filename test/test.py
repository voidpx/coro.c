import subprocess as sp, datetime as dt

def run_test(times):
	for i in range(times):
		out = sp.run('build/test', capture_output=True, text=True).stdout
		assert out.find('start sleep') >= 0
		assert out.find('end sleep') >= 0
		assert out.find('task999 return:999') >= 0
		print(f'{dt.datetime.now()} test run {i} successful')
		

if __name__ == "__main__":
	run_test(100)
	
