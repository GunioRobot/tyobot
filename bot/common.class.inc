<?php
namespace tyobot;

/**
 * common Bot クラス
 *
 **/

include_once "conf/common.conf.inc";

include_once "XML/RSS.php";
include_once 'Services/Amazon.php';
require_once 'XML/Unserializer.php';
include_once "include/simple_html_dom.php";
include_once 'HTTP/OAuth/Consumer.php';

require_once 'Zend/Loader.php';
	\Zend_Loader::loadClass('Zend_Gdata');
	\Zend_Loader::loadClass('Zend_Gdata_AuthSub');
	\Zend_Loader::loadClass('Zend_Gdata_ClientLogin');
	//Zend_Loader::loadClass('Zend_Gdata_Docs');
	\Zend_Loader::loadClass('Zend_Gdata_Spreadsheets');
	\Zend_Loader::loadClass('Zend_Gdata_YouTube');
	\Zend_Loader::loadClass('Zend_Service_Twitter');

include_once "./bot/base.class.inc";

class tyobot_common extends tyobot_base {

	var $amazon;
	var $google_client;
	var $google_spreadseets;
	var $google_docs;
	var $google_youtube;

	var $twitter_oauth;

	var $conn = null;
	
	function __construct( &$conn ) {
		parent::__construct();
		$this->amazon = new \Services_Amazon(AMAZON_API_ACCESS_KEY, AMAZON_API_SECRET_KEY, AMAZON_ASSOCIATE_TAG);
		$this->amazon->setLocale('JP');

		$this->google_client = \Zend_Gdata_ClientLogin::getHttpClient( GOOGLE_USER, GOOGLE_PASS, \Zend_Gdata_Spreadsheets::AUTH_SERVICE_NAME );
		//$this->google_docs = new \Zend_Gdata_Docs($this->google_client);
		$this->google_spreadseets = new \Zend_Gdata_Spreadsheets($this->google_client);
		$this->google_youtube = new \Zend_Gdata_YouTube();
		$this->conn = $conn;
	}

	function _setCommand( &$irc ){
		// BOT を終了する$irc->registerActionhandler(SMARTIRC_TYPE_CHANNEL, '^' . BOT_NICK . DELIMITER . COMMAND_QUIT, $bot_c, COMMAND_QUIT);

		// URLのタイトルを表示
		$irc->registerActionhandler(SMARTIRC_TYPE_CHANNEL, URL_PATTERN , $this, COMMAND_URL );

		// twitter
		$irc->registerActionhandler(SMARTIRC_TYPE_CHANNEL, '^'. TWITTER_CMD . ' ', $this, COMMAND_TWITTER );

		// facebook
		$irc->registerActionhandler(SMARTIRC_TYPE_CHANNEL, '^'. FACEBOOK_CMD . ' ', $this, COMMAND_FACEBOOK );

		// Google
		$irc->registerActionhandler(SMARTIRC_TYPE_CHANNEL, '^'. GOOGLE_CMD . ' ', $this, COMMAND_GOOGLE );

		// ustream
		$irc->registerActionhandler(SMARTIRC_TYPE_CHANNEL, '^'. USTREAM_CMD .' ', $this, COMMAND_USTREAM );

		// amazon
		$irc->registerActionhandler(SMARTIRC_TYPE_CHANNEL, '^'. AMAZON_CMD . ' ', $this, COMMAND_AMAZON );
		
		// calculator
		$irc->registerActionhandler(SMARTIRC_TYPE_CHANNEL, '^'. CALCULATOR_CMD . ' ', $this, COMMAND_CALCULATOR );

		// alias
		$irc->registerActionhandler(SMARTIRC_TYPE_CHANNEL, '^'. ALIAS_CMD . ' ', $this, COMMAND_ALIAS );

		// inviteされた
		$irc->registerActionhandler(SMARTIRC_TYPE_INVITE, '.*', $this, '__invited__' );

		// omikujiされた
		//$irc->registerActionhandler(SMARTIRC_TYPE_CHANNEL, '^!omikuji', $this, 'omikuji' );

		// 外部システムとの連携用に特定ファイルに書き込まれた文字をircに書き込み
		$irc->registerTimehandler( 10000, $this, 'checkPost');
	}

	/**
	 * URL検出
	 */
	function url(&$irc, &$data) {
		$message = $this->_2utf8($data->message );
		if (!(preg_match_all('/'.URL_PATTERN.'/', $message, $matches))) {
			$this->_notice($irc, URL_ERR_MSG, $data->channel );
		} else {
			$urls = $matches[0];
			d(memory_get_usage());
			foreach ($urls as $url) {
				$str = $this->get_url_string( $url );
				d(memory_get_usage());
				if( $str ){
					$this->_notice($irc, $str, $data->channel);
				}
			}
		}
	}

	function get_url_string($url) {
		try{
			if( preg_match( URL_WIKIPEDIA_TOPPAGE_LINK_REGEX, $url ) ){
				//日本語URLが取得できていない場合。
				return false;
			}
			$str = "";
			$heads = my_get_headers($url, 1);
			if( $heads === FALSE  ){ return '■get respons error.'; }
			if( $heads['count'] > 0 ){
				if( $heads['last-status'] == 301 || $heads['last-status'] == 302 ){
					if( $heads['loop']){
						return '■Redirecting has looped.';
					}
					return '■It redirects it three times or more. ';
				}
				$url = $heads[ 'last-location'];
				$str .= '▼Redirect to '.$url."\n";
			}
			if( $heads['last-status'] != 200 ){
				return '■'.$heads[ $heads['count'] ]['status-line'];
			}
			if (preg_match_all(URL_AMAZON_ITEM_LINK_REGEX, $url, $matches)) {
				$asin = $matches[2][0];
				$item = $this->amazon->ItemLookup($asin, Array());
				if (is_array($item)) {
					$str .= "【A】(".$item['Item'][0]['ItemAttributes']['Title'].") ".URL_AMAZON_TINY_URL.$asin."\n";
				}
			} else if (preg_match_all(URL_NICONICO_WATCH_LINK_REGEX, $url, $matches)) {
				$api_url = 'http://ext.nicovideo.jp/api/getthumbinfo/'.$matches[1][0];
				$xml = file_get_contents($api_url);
				$Unserializer =& new \XML_Unserializer(Array('parseAttributes' => TRUE));
				$xml_status = $Unserializer->unserialize($xml);
				if ($xml_status) {
					$api_array = $Unserializer->getUnserializedData();
					if ($api_array['status'] == ok) {
						$str .= '【ニコ】'.$api_array['thumb']['title'].'['.$api_array['thumb']['length'].'](再生:'.$api_array['thumb']['view_counter'].")\n";
					}
				}
			} else if (preg_match_all(URL_YOUTUBE_WATCH_LINK_REGEX, $url, $matches)) {
				$entry = $this->google_youtube->getVideoEntry( $matches[1][0] );
				$str .= '【Y】'.$entry->getVideoTitle().'['.$entry->getVideoCategory().']';
			}else if (preg_match_all(URL_GOOGLE_SPREADSHEETS_LINK_REGEX, $url, $matches)) {
				$query = new \Zend_Gdata_Spreadsheets_DocumentQuery();
				$query->setSpreadsheetKey($matches[1][0] );
				$worksheet_feed = $this->google_spreadseets->getWorksheetFeed( $query );
				$str = '【G-s】'.$worksheet_feed->title->text;
			}else if (preg_match_all(URL_TWITTER_STATUS_LINK_REGEX, $url, $matches)) {
            	$twitter = new \Zend_Service_Twitter($this->twitter_oauth);
				$status = $twitter->status->show($matches[5][0]);
				$str = '【Tw】'.$status->user->name.'「'.$status->text.'」';
			}else if (preg_match_all(URL_TWITTER_USER_LINK_REGEX, $url, $matches)) {
            	$twitter = new \Zend_Service_Twitter($this->twitter_oauth);
				$user = $twitter->user->show($matches[3][0]);
				$str = $user->name.' ('.$user->screen_name.') '.$user->url.' http://twitter.com/'.$user->screen_name.' bio:'.to_short($user->description, 40)."\n"."followers:".$user->followers_count.' following:'.$user->friends_count.' status:'.$user->statuses_count.' fav:'.$user->favourites_count;
			}else{
				$head = $heads[$heads['count']];
				$ct = explode(' ', $head['content-type']);
				$types = explode('/', str_replace(';', '/', $ct[0]));
				switch ($types[0]) {
					case 'text':
						if ($types[1] == 'html') {
							$html = file_get_html($url);
							d(memory_get_usage());
							$titles = $html->find('title', 0);
							if (strlen($titles->innertext )) {
								$str .= to_short('■'.preg_replace('/\s\s+/', ' ', str_replace(array("\r\n", "\n", "\r"), '', trim($this->_2utf8($titles->innertext)))), URL_TITLE_MAX_LENGTH);
								$str .= '['.$ct[0].']';
								$html->clear();
								break;
							}
							$html->clear();
						}
					default:
						$str .= "■".$ct[0];
						if (!empty($head['content-length'])) {
							$str .= '('.ConvertUnit($head['content-length']).'B)';
						}
						break;
					case 'image':
						$size = getimagesize($url);
						$str .= "■".$ct[0];
						if (!empty($head['content-length'])) {
							$str .= '('.ConvertUnit($head['content-length']).'B)';
						}
						if ($size[0] && $size[1]) {
							$str .= '('.$size[0].'*'.$size[1].')';
						}
						break;
				}
			}
		} catch (\Services_Twitter_Exception $e) {
			switch ($e->getCode()) {
				case 404:
				case 403:
					$str = 'this twitter request is '.$e->getMessage();
					break;
				default:
					$str = 'this twitter request to code:'.$e->getCode();
			}
		}
		return $str;
	}

	function google(&$irc, &$data) {
		$message = $this->_2utf8($data->message );
		if( $data->nick  == 'kakuge_anon' ){
			$matches = array();
			if( preg_match( '/(.*)\(CV\:.*\)/i', $message ,$matches ) ){
				$message = $matches[1];
			}
		}
		$text = substr($message, 2);
		d(memory_get_usage());
		$html = file_get_html('http://www.google.com/search?ls=ja&hl=ja&oe=utf-8&q='.urlencode($text));
		$title = $html->find('title', 0);
		$str = '▼'.$this->_2utf8($title->innertext ).'('.$html->find('p[id=resultStats]/b', 1)->innertext.")\n";
		$list = $html->find('li');
		$i = 1;
		foreach ($list as $e) {
			$str .= strip_tags('■'.$this->_2utf8($e->find('h3[class=r]/a', 0)->innertext ));
			$str .= ':'.$this->_2utf8($e->find('a[class=l]', 0)->href )."\n";
			$desc = $this->_2utf8($e->find('div[class=s]', 0)->innertext );
			$desc = strip_tags(substr($desc, 0, strpos($desc, '<br>')));
			$desc = to_short($desc, GOOGLE_DESCRIPTION_LENGTH);
			$str .= $desc."\n";

			if ($i++ >= GOOGLE_CNT) {
				break;
			}
		}
		$this->_notice($irc, $str, $data->channel );
		d(memory_get_usage());
		$html->clear();
	}

	function calculator(&$irc, &$data) {
		$message = $this->_2utf8($data->message );
		$text = substr($message, strlen(CALCULATOR_CMD)+1);
		$m = Array();
		if (preg_match('/[^0123456789()+-\/\*\% ]/', $text, $m)) {
			$this->_notice($irc, CALCULATOR_MSG, $data->channel);
			return;
		}
		eval('$ret = '."$text;" );
		$this->_notice($irc, $ret, $data->channel);
	}

	function twitter(&$irc, &$data) {
		$args = $this->_command( $this->_2utf8($data->message), TWITTER_CMD.' ' );
		$id = $args[0];
		$str = "";
		try {
            $twitter = new \Zend_Service_Twitter($this->twitter_oauth);
			if (isset($args[1]) && is_numeric($args[1])) {
				$cnt = $args[1];
				if ($cnt > TWITTER_TIMELINE_MAX) {
					$cnt = TWITTER_TIMELINE_MAX;
				}
				$timelines = $twitter->status->userTimeline(Array('id' => $id, 'count' => $cnt));
				foreach ($timelines as $timeline) {
					$str .= date('m/d h:i', strtotime($timeline->created_at)).':'.$timeline->text."\n";
				}
			} else {
				$user = $twitter->user->show($id);
				$str .= $user->name.' ('.$user->screen_name.') '.$user->url.' http://twitter.com/'.$user->screen_name.' bio:'.to_short($user->description, 40)."\n"."followers:".$user->followers_count.' following:'.$user->friends_count.' status:'.$user->statuses_count.' fav:'.$user->favourites_count;
			}
			$this->_notice($irc, $str, $data->channel);
		} catch ( Zend_Service_Twitter_Exception $e) {
			switch ($e->getCode()) {
				case 404:
				case 403:
					$this->_notice($irc, 'this user is '.$e->getMessage(), $data->channel);
					break;
				default:
					$this->_notice($irc, 'this twitter request to code:'.$e->getCode(), $data->channel);
			}
		}
	}

	function facebook(&$irc, &$data) {
		$args = $this->_command( $this->_2utf8($data->message), FACEBOOK_CMD.' ' );
		$id = $args[0];
		$str = "";
		try {
			$user =  file_get_contents( 'https://graph.facebook.com/'.$id.'?access_token='.FACEBOOK_APP_ACCESS_TOKEN);
			$user_data = json_decode( $user );
			if( !isset($user_data->error) ){
				$str = $id.'('.$user_data->name.') '. $user_data->link.' '.$user_data->gender.' '.mb_ereg_replace("\n",' ',$user_data->about);
			}else{
				$str = FACEBOOK_MSG;
			}
			$this->_notice($irc, $str, $data->channel);

		} catch (FacebookApiException $e) {
			d($e);
		}
	}

	function ustream(&$irc, &$data) {
		$args = $this->_command( $this->_2utf8($data->message), USTREAM_CMD.' ' );
		$url = USTREAM_URL.$args[0];
		$heads = get_headers($url, 1);
		$status = strstr($heads[0], ' ');
		if ((int) $status != 200) {
			$this->_notice($irc, USTREAM_MSG_ERROR, $data->channel );
		} else {
			$this->_notice($irc, $url, $data->channel );
		}
	}

	/**
	 * Amazon 検索
	 *
	 */

	function amazon(&$irc, &$data) {
		$args = $this->_command( $this->_2utf8($data->message), AMAZON_CMD.' ' );
		$str = "";
		$result = $this->amazon->ItemSearch('All', Array('Keywords' => join(' ',$args)));
		if (is_array($result)) {
			foreach (array_splice($result['Item'], 0, AMAZON_CNT) as $item) {
				$str .= '■['.$item['ItemAttributes']['ProductGroup'].']'.$item['ItemAttributes']['Title'];
				$str .= ' '.URL_AMAZON_TINY_URL.$item['ASIN']."\n";
			}
		} else {
			$str .= '■'.AMAZON_MSG_ERROR;
		}
		$this->_notice($irc, $str, $data->channel );
	}

	/**
	 * Alias
	 *
	 */
	function alias(&$irc, &$data) {
		$args = $this->_command( $this->_2utf8($data->message), ALIAS_CMD.' ' );
		$text = $args[0];
		if(strlen($text)){
			$rs = &$this->conn->Execute('select * from alias WHERE alias = '.$this->conn->qstr($text));
			d( 'select * from alias WHERE alias = '.$this->conn->qstr($text));
			if(!$rs){ d($this->conn->ErrorMsg());}
			else if( $rs->RecordCount() ){
				$row = $rs->FetchRow();
				$this->_notice( $irc, $row['string'], $data->channel );
			}else{
				$this->_notice( $irc, ALIAS_MSG_ERROR, $data->channel );
			}
		}

	}

	function checkPost( &$irc ){
		if( defined( 'AUTO_IRC_MSG_FILE' ) ){
			$fp = fopen(AUTO_IRC_MSG_FILE,'r+');
			if( !$fp ){ return; } 
			$buffer = array();
			while ($data=fgets($fp, 10000)){
				$buffer[] = $data; 
			}
			$first = array_shift( $buffer );

			ftruncate($fp,0);
			fseek( $fp, 0 );

			foreach( $buffer as $row ){
				fputs( $fp, $row );
			}

			fclose( $fp );
			if( !is_null( $first ) ){
				$channel = substr( $first, 0, strpos( $first, ' ' ) );
				$str = substr( $first, strpos( $first, ' ' )+1 );
				$this->_notice( $irc, $str, $channel );
			}
		}
	}

	function omikuji(&$irc, &$data) {
		$list = array(
			28 =>'【大吉】',
			42 => '【中吉】',
			56 => '【吉】',
			67 => '【小吉】',
			78 => '【末吉】',
			85 => '【凶】',
			92 => '【大凶】',
			94 => '【ぴょん吉】',
			96 => '【だん吉】',
			97 => '【とらきち】',
			98 => '【豚】',
			99=>'【神】',
			100=>'【女神】');
		$num = rand(0,100);
		foreach( $list as $key => $val){
			if( $num <= $key ){
				$str = $data->nick."さんは...".$val."です！";
				$this->_notice( $irc, $str, $data->channel );
				return;
			}
		}
	
	}

	/*
	 * BOT を終了する
	 *
	 *
	 */

	function quit(&$irc) {
		$irc->quit($this->_convert(QUIT_MSG));
	}

}
?>
